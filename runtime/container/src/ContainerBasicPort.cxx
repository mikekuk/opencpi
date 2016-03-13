/*
 *  This file is part of OpenCPI (www.opencpi.org).
 *     ____                   __________   ____
 *    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
 *   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
 *  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
 *  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
 *      /_/                                             /____/
 *
 *  OpenCPI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCPI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>
#include "OcpiOsAssert.h"
#include "OcpiUtilCDR.h"
#include "Container.h"
//#include "ContainerWorker.h"
#include "ContainerPort.h"

namespace OCPI {
  namespace Container {
    namespace OA = OCPI::API;
    namespace OU = OCPI::Util;
    namespace OD = OCPI::DataTransport;
    namespace OR = OCPI::RDT;

    PortData::
    PortData(const OU::Port &mPort, PortConnectionDesc *desc)
      : m_ordinal(mPort.m_ordinal), m_isProvider(mPort.m_provider), m_connectionData(desc)
    {
      OR::Descriptors &d = getData().data;
      d.type = m_isProvider ? OR::ConsumerDescT : OR::ProducerDescT;
      d.role = OR::NoRole;
      d.options = 0;
      bzero((void *)&d.desc, sizeof(d.desc));
      size_t nBuffers =
	mPort.m_defaultBufferCount == SIZE_MAX ? DEFAULT_NBUFFERS : mPort.m_defaultBufferCount;
      m_nBuffers = nBuffers > mPort.m_minBufferCount ? nBuffers : mPort.m_minBufferCount;
      // FIXME: this is really set at connection time and should be removed here and left
      // == SIZE_MAX
      if (mPort.m_bufferSize == SIZE_MAX) {
	m_bufferSize = OU::DEFAULT_BUFFER_SIZE;
	ocpiDebug("PortData %s(%p): setting buffer size from default: %zu",
		  mPort.m_name.c_str(), this, OU::DEFAULT_BUFFER_SIZE);
      } else {
	m_bufferSize = mPort.m_bufferSize;
	ocpiDebug("PortData %s(%p): setting buffer size from metadata: %zu",
		  mPort.m_name.c_str(), this, mPort.m_bufferSize);
      }
      d.desc.nBuffers = OCPI_UTRUNCATE(uint32_t, m_nBuffers);
      d.desc.dataBufferSize = OCPI_UTRUNCATE(uint32_t, m_bufferSize);
      ocpiDebug("PortData final nbuffers %zu bufsize %zu", m_nBuffers, m_bufferSize);
    }

    static bool findRole(const OU::PValue *params, const char *&s, OR::PortRole &role) {
      if (OU::findString(params, "xferRole", s)) {
	if (!strcasecmp(s, "passive"))
	  role = OR::Passive;
	else if (!strcasecmp(s, "active") ||
		 !strcasecmp(s, "activemessage"))
	  role = OR::ActiveMessage;
	else if (!strcasecmp(s, "flowcontrol") ||
		 !strcasecmp(s, "activeflowcontrol"))
	  role = OR::ActiveFlowControl;
	else if (!strcasecmp(s, "activeonly"))
	  role = OR::ActiveOnly;
	else
	  throw OU::Error("xferRole property must be passive|active|flowcontrol|activeonly");
	return true;
      }
      return false;
    }

    ExternalBuffer::
    ExternalBuffer(BasicPort &port, ExternalBuffer *next, unsigned n)
      : m_port(port), m_full(false), m_position(n), m_next(next), m_zcFront(NULL),
	m_zcBack(NULL), m_dtBuffer(NULL), m_dtData(NULL) {
      memset(&m_hdr, 0, sizeof(m_hdr));
    }

    size_t ExternalBuffer::offset() {
      return m_position * m_port.m_bufferStride;
    }

    // ZC put
    void ExternalBuffer::
    put(ExternalPort &port) {
      if (&port == &m_port)
	put();
      else
	port.put(*this);
    }

    // ZC put
    void ExternalBuffer::
    put(ExternalPort &port, size_t length, uint8_t opCode, bool end, size_t direct) {
      m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, length);
      m_hdr.m_opCode = opCode;
      m_hdr.m_eof    = end ? 1 : 0;
      m_hdr.m_direct = OCPI_UTRUNCATE(uint8_t, direct);
      put(port);
    }

    BasicPort::
    BasicPort(Container &c, const OU::Port &metaData, const OU::PValue *params)
      : PortData(metaData, NULL), m_lastInBuffer(NULL), m_lastOutBuffer(NULL),
	m_dtLastBuffer(*this, NULL, 0), m_dtPort(NULL), m_allocation(NULL), m_bufferStride(0),
	m_next2write(NULL), m_next2put(NULL), m_next2read(NULL), m_next2release(NULL),
	m_forward(NULL), m_backward(NULL), m_nRead(0), m_nWritten(0), myDesc(getData().data.desc),
	m_metaPort(metaData), m_container(c) {
      applyPortParams(params);
    }

    BasicPort::
    ~BasicPort(){
      OU::SelfAutoMutex guard(this);
      if (m_backward) {
	// If we are being forwarded-to, we need to break this chain, while
	// the other side is not in the middle of forwarding to us.
	OU::SelfAutoMutex guard(m_backward);
	m_backward->m_forward = NULL;
      } else if (m_forward) {
	OU::SelfAutoMutex guard(m_forward);
	m_forward->m_backward = NULL;
      }
      if (m_dtPort)
	m_dtPort->reset();
      if (m_allocation && m_allocator == this)
	freeBuffers(m_allocation);
    }

    void BasicPort::
    applyPortParams(const OU::PValue *params) {
      OA::ULong ul;
      if (OU::findULong(params, "bufferCount", ul))
	if (ul < m_metaPort.m_minBufferCount)
	  throw OU::Error("bufferCount is below worker's minimum");
        else {
	  getData().data.desc.nBuffers = OCPI_UTRUNCATE(uint32_t, (m_nBuffers = ul));
	  ocpiDebug("Setting nbuffers from parameters on \"%s\" to %zu",
		    name().c_str(), m_nBuffers);
	}
    }

    /*
     * ----------------------------------------------------------------------
     * A simple test.
     * ----------------------------------------------------------------------
     */
    /*
      static int
      pack_unpack_test (int argc, char *argv[])
      {
      OR::Descriptors d;
      std::string data;
      bool good;

      std::memset (&d, 0, sizeof (OR::Descriptors));
      d.mode = OR::ConsumerDescType;
      d.desc.c.fullFlagValue = 42;
      std::strcpy (d.desc.c.oob.oep, "Hello World");
      data = packDescriptor (d);
      std::memset (&d, 0, sizeof (OR::Descriptors));
      good = unpackDescriptor (data, d);
      ocpiAssert (good);
      ocpiAssert (d.mode == OR::ConsumerDescType);
      ocpiAssert (d.desc.c.fullFlagValue == 42);
      ocpiAssert (std::strcmp (d.desc.c.oob.oep, "Hello World") == 0);

      std::memset (&d, 0, sizeof (OR::Descriptors));
      d.mode = OR::ProducerDescType;
      d.desc.p.emptyFlagValue = 42;
      std::strcpy (d.desc.p.oob.oep, "Hello World");
      data = packDescriptor (d);
      std::memset (&d, 0, sizeof (OR::Descriptors));
      good = unpackDescriptor (data, d);
      ocpiAssert (good);
      ocpiAssert (d.mode == OR::ProducerDescType);
      ocpiAssert (d.desc.p.emptyFlagValue == 42);
      ocpiAssert (std::strcmp (d.desc.p.oob.oep, "Hello World") == 0);

      data[0] = ((data[0] == '\0') ? '\1' : '\0'); // Hack: flip byteorder
      good = unpackDescriptor (data, d);
      ocpiAssert (!good);

      return 0;
      }
    */
    static void putOffset(OU::CDR::Encoder &packer, DtOsDataTypes::Offset val) {
      packer.
#if OCPI_EP_SIZE_BITS == 64
      putULongLong(val);
#else
      putULong(val);
#endif
    }
    static void putFlag(OU::CDR::Encoder &packer, DtOsDataTypes::Flag val) {
      packer.
#if OCPI_EP_FLAG_BITS == 64
      putULongLong(val);
#else
      putULong(val);
#endif
    }
    static void getOffset(OU::CDR::Decoder &unpacker, DtOsDataTypes::Offset &val) {
      unpacker.
#if OCPI_EP_SIZE_BITS == 64
      getULongLong(val);
#else
      getULong(val);
#endif
    }
    static void getFlag(OU::CDR::Decoder &unpacker, DtOsDataTypes::Flag &val) {
      unpacker.
#if OCPI_EP_FLAG_BITS == 64
      getULongLong(val);
#else
      getULong(val);
#endif
    }

    void BasicPort::
    packPortDesc(const OR::Descriptors & desc, std::string &out) throw() {
      OU::CDR::Encoder packer;
      packer.putBoolean (OU::CDR::nativeByteorder());
      packer.putULong     (desc.type);
      packer.putULong     (desc.role);
      packer.putULong     (desc.options);
      const OR::Desc_t & d = desc.desc;
      packer.putULong     (d.nBuffers);
      putOffset(packer, d.dataBufferBaseAddr);
      packer.putULong     (d.dataBufferPitch);
      packer.putULong     (d.dataBufferSize);
      putOffset(packer, d.metaDataBaseAddr);
      packer.putULong     (d.metaDataPitch);
      putOffset(packer, d.fullFlagBaseAddr);
      packer.putULong     (d.fullFlagSize);
      packer.putULong     (d.fullFlagPitch);
      putFlag(packer, d.fullFlagValue);
      putOffset(packer, d.emptyFlagBaseAddr);
      packer.putULong     (d.emptyFlagSize);
      packer.putULong     (d.emptyFlagPitch);
      putFlag(packer, d.emptyFlagValue);
      packer.putULongLong (d.oob.port_id);
      packer.putString    (d.oob.oep);
      packer.putULongLong (d.oob.cookie);
      out = packer.data();
    }

    bool BasicPort::
    unpackPortDesc(const std::string &data, OR::Descriptors &desc) throw () {
      OU::CDR::Decoder unpacker (data);

      try { 
	bool bo;
	unpacker.getBoolean (bo);
	unpacker.byteorder (bo);
        unpacker.getULong (desc.type);
        unpacker.getLong (desc.role);
        unpacker.getULong (desc.options);
	OR::Desc_t & d = desc.desc;
	unpacker.getULong     (d.nBuffers);
	getOffset(unpacker, d.dataBufferBaseAddr);
	unpacker.getULong     (d.dataBufferPitch);
	unpacker.getULong     (d.dataBufferSize);
	getOffset(unpacker, d.metaDataBaseAddr);
	unpacker.getULong     (d.metaDataPitch);
	getOffset(unpacker, d.fullFlagBaseAddr);
	unpacker.getULong     (d.fullFlagSize);
	unpacker.getULong     (d.fullFlagPitch);
	getFlag(unpacker, d.fullFlagValue);
	getOffset(unpacker, d.emptyFlagBaseAddr);
	unpacker.getULong     (d.emptyFlagSize);
	unpacker.getULong     (d.emptyFlagPitch);
	getFlag(unpacker, d.emptyFlagValue);
	unpacker.getULongLong (d.oob.port_id);
        std::string oep;
	unpacker.getString (oep);
        if (oep.length()+1 > sizeof(d.oob.oep))
          return false;
	unpacker.getULongLong (d.oob.cookie);
        std::strcpy (d.oob.oep, oep.c_str());
      }
      catch (const OU::CDR::Decoder::InvalidData &) {
	return false;
      }
      return true;
    }

    static void defaultRole(OR::PortRole &role, unsigned options) {
      if (role == OR::NoRole) {
	for (unsigned n = 0; n < OR::MaxRole; n++)
	  if (options & (1 << n)) {
	    role = (OR::PortRole)n;
	    return;
	  }
	throw OU::Error("Container port has no transfer roles");
      }
    }

    // coming in, specified roles are preferences or explicit instructions.
    // The existing settings are either NoRole, a preference, or a mandate
    // static method
    const char *BasicPort::
    chooseRoles(OR::PortRole &uRole, unsigned uOptions, OR::PortRole &pRole, unsigned pOptions)
    {
      // FIXME this relies on knowledge of the values of the enum constants
      static OR::PortRole otherRoles[] = { OCPI_RDT_OTHER_ROLES };
      defaultRole(uRole, uOptions);
      defaultRole(pRole, pOptions);
      OR::PortRole
        pOther = otherRoles[pRole],
        uOther = otherRoles[uRole];
      if (pOptions & (1 << OR::MandatedRole)) {
        // provider has a mandate
        ocpiAssert(pRole != OR::NoRole);
        if (uRole == pOther)
          return NULL;
        if (uOptions & (1 << OR::MandatedRole))
          return "Incompatible mandated transfer roles";
        if (uOptions & (1 << pOther)) {
          uRole = pOther;
          return NULL;
        }
        return "No compatible role available against mandated role";
      } else if (pRole != OR::NoRole) {
        // provider has a preference
        if (uOptions & (1 << OR::MandatedRole)) {
          // user has a mandate
          ocpiAssert(uRole != OR::NoRole);
          if (pRole == uOther)
            return NULL;
          if (pOptions & (1 << uOther)) {
            pRole = uOther;
            return NULL;
          }
          throw OU::Error("No compatible role available against mandated role");
        } else if (uRole != OR::NoRole) {
          // We have preferences on both sides, but no mandate
          // If preferences match, all is well
          if (pRole == uOther)
            return NULL;
          // If one preference is against push, we better listen to it.
          if (uRole == OR::ActiveFlowControl &&
              pOptions & (1 << OR::ActiveMessage)) {
            pRole = OR::ActiveMessage;
            return NULL;
          }
          // Let's try active push if we can
          if (uRole == OR::ActiveMessage &&
              pOptions & (1 << OR::ActiveFlowControl)) {
            pRole = OR::ActiveFlowControl;
            return NULL;
          }
          if (pRole == OR::ActiveFlowControl &&
              uOptions & (1 << OR::ActiveMessage)) {
            uRole = OR::ActiveFlowControl;
            return NULL;
          }
          // Let's try activeonly push if we can
          if (uRole == OR::ActiveOnly &&
              pOptions & (1 << OR::Passive)) {
            pRole = OR::Passive;
            return NULL;
          }
          if (pRole == OR::Passive &&
              pOptions & (1 << OR::ActiveOnly)) {
            pRole = OR::ActiveOnly;
            return NULL;
          }
          // Let's give priority to the "better" role.
          if (uRole < pRole &&
              pOptions & (1 << uOther)) {
            pRole = uOther;
            return NULL;
          }
          // Give priority to the provider
          if (uOptions & (1 << pOther)) {
            uRole = pOther;
            return NULL;
          }
          if (pOptions & (1 << uOther)) {
            pRole = uOther;
            return NULL;
          }
          // Can't use either preference.  Fall throught to no mandates, no preferences
        } else {
          // User role unspecified, but provider has a preference
          if (uOptions & (1 << pOther)) {
            uRole = pOther;
            return NULL;
          }
          // Can't use provider preference, Fall through to no mandates, no preferences
        }
      } else if (uOptions & (1 << OR::MandatedRole)) {
        // Provider has no mandate or preference, but user has a mandate
        if (pOptions & (1 << uOther)) {
          pRole = uOther;
          return NULL;
        }
        return "No compatible role available against mandated role";
      } else if (uRole != OR::NoRole) {
        // Provider has no mandate or preference, but user has a preference
        if (pOptions & (1 << uOther)) {
          pRole = uOther;
          return NULL;
        }
        // Fall through to no mandates, no preferences.
      }
      // Neither has useful mandates or preferences.  Find anything, biasing to push
      for (unsigned i = 0; i < OR::MaxRole; i++)
        // Provider has no mandate or preference
        if (uOptions & (1 << i) &&
            pOptions & (1 << otherRoles[i])) {
          uRole = (OR::PortRole)i;
          pRole = otherRoles[i];
          return NULL;
        }
      return "No compatible combination of roles exist";
    }
    // Figure out the transport, the interconnect instance id, and roles for a connection.
    // Port parameters override connection parameters that act as a default for all
    // ports on the connection.
    // static method
    void BasicPort::
    determineTransport(const Transports &in, const Transports &out,
		       const OU::PValue *paramsIn, const OU::PValue *paramsOut,
		       const OU::PValue *paramsConn, Transport &transport) {
      // Giving priority to the input side in a tie, find the first transport that
      // both sides support that have compatible roles.
      static const char *roleNames[] = { OCPI_RDT_ROLE_NAMES };
      OR::PortRole roleIn = OR::NoRole, roleOut = OR::NoRole;
      const char *s;
      findRole(paramsIn, s, roleIn); // overrides per port for xfer role
      findRole(paramsOut, s, roleOut);
      const char *err = NULL, *tIn, *tOut, *tConn;
      std::string sIn, sOut, sConn;
      if (OU::findString(paramsConn, "transport", tConn)) {
	if (!strchr(tConn, '-'))
	  OU::format(sConn, "ocpi-%s-rdma", tConn);
	else
	  sConn = tConn;
      }
      if (OU::findString(paramsIn, "transport", tIn)) {
	if (!strchr(tIn, '-'))
	  OU::format(sIn, "ocpi-%s-rdma", tIn);
	else
	  sIn = tIn;
      }
      if (OU::findString(paramsOut, "transport", tOut)) {
	if (!strchr(tOut, '-'))
	  OU::format(sOut, "ocpi-%s-rdma", tOut);
	else
	  sOut = tOut;
      }
      if (sConn.length()) {
	if (sIn.length() && strcasecmp(sIn.c_str(), sConn.c_str()) ||
	    sOut.length() && strcasecmp(sOut.c_str(), sConn.c_str()))
	  throw OU::Error("Inconsistent transports: connection \"%s\" out \"%s\" in \"%s\"",
			  tConn, tIn, tOut);
	sIn = sOut = sConn;
      } else if (sIn.length()) {
	if (sOut.length() && strcasecmp(sIn.c_str(), sOut.c_str()))
	  throw OU::Error("Inconsistent transports: out \"%s\" in \"%s\"",
			  tIn, tOut);
	sConn = sIn;
      } else
	sConn = sOut;
      for (unsigned ni = 0; ni < in.size(); ni++) {
	const Transport &it = in[ni];
	if (sConn.length() && strcasecmp(sConn.c_str(), it.transport.c_str()))
	  ocpiInfo("Rejecting input transport %s since %s was specified for the connection",
		   it.transport.c_str(), sConn.c_str());
	else if (roleIn != OR::NoRole && !((1 << roleIn) & it.optionsIn))
	  ocpiInfo("Rejecting input role %s for transport %s: container doesn't support it",
		   roleNames[roleIn], it.transport.c_str());
	else
	  // fall through - an acceptable possible input
	  for (unsigned no = 0; no < out.size(); no++) {
	    const Transport &ot = out[no];
	    if (strcasecmp(it.transport.c_str(), ot.transport.c_str()))
	      ;
	    else if (strcasecmp(it.id.c_str(), ot.id.c_str()))
	      ocpiInfo("Rejecting output transport %s since input id is %s but output id is %s",
		       ot.transport.c_str(), it.id.c_str(), ot.id.c_str());
	    else if (sConn.length() && strcasecmp(sConn.c_str(), ot.transport.c_str()))
	      ocpiInfo("Rejecting out transport %s since %s was specified for the connection",
		       ot.transport.c_str(), sConn.c_str());
	    else if (roleOut != OR::NoRole && !((1 << roleOut) & ot.optionsOut))
	      ocpiInfo("Rejecting input role %s for transport %s: container doesn't support it",
		       roleNames[roleOut], ot.transport.c_str());
	    else {
	      // Everything matches
	      transport.roleIn = it.roleIn;
	      transport.roleOut = ot.roleOut;
	      if ((err = chooseRoles(transport.roleOut, ot.optionsOut, transport.roleIn,
				     it.optionsIn)))
		ocpiInfo("Rejecting transport %s since role support is incompatible",
			 it.transport.c_str());
	      else {
		transport.transport = it.transport;
		transport.id = it.id;
		transport.optionsIn = transport.optionsOut = OR::MandatedRole;
		ocpiInfo("Choosing transport %s/%s for connection",
			 it.transport.c_str(), it.id.c_str());
		return;
	      }
	    }
	  }
      }
      if (err)
	throw OU::Error("Error choosing transfer roles: %s", err);
      else
	throw OU::Error("No compatible transports for connection");
    }

    // Step 1: get an empty buffer to fill.
    ExternalBuffer *BasicPort::
    getEmptyBuffer() {
      if (m_forward)
	return m_forward->getEmptyBuffer();
      if (m_next2write) { // shim mode
	//	ocpiDebug("getempty: %p %p %u", this, m_next2write, m_next2write->m_full);
	if (!m_next2write->m_full) {
	  m_next2write->m_hdr.m_data =
	    OCPI_UTRUNCATE(uint8_t,
			   sizeof(ExternalBuffer) - OCPI_OFFSETOF(ExternalBuffer, m_hdr));
	  m_next2write->m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, m_bufferSize);
	  ExternalBuffer *b = m_next2write;
	  m_next2write = m_next2write->m_next;
	  ocpiDebug("GetEmpty on %p returns %p", this, b);
	  return b;
	}
	return NULL;
      }
      size_t length;
      if (m_dtPort &&
	  (m_dtLastBuffer.m_dtBuffer =
	   m_dtPort->getNextEmptyOutputBuffer(m_dtLastBuffer.m_dtData, length))) {
	m_dtLastBuffer.m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, length);	
	return &m_dtLastBuffer;
      }
      return NULL;
    }

    // Step 1: high level API
    OA::ExternalBuffer *BasicPort::
    getBuffer(uint8_t *&data, size_t &length) {
      if (isProvider())
	throw OU::Error("getBuffer for output port called on input port \"%s\"",
			name().c_str());
      if ((m_forward ? m_forward : this)->m_lastOutBuffer)
	throw OU::Error("getBuffer called on output port %s without putting previous buffer",
			name().c_str());
      ExternalBuffer *b = getEmptyBuffer();
      if (b) {
	data = b->data();
	length = b->m_hdr.m_length;
	(m_forward ? m_forward : this)->m_lastOutBuffer = b;
      }
      return b;
    }

    void ExternalBuffer::
    send(size_t length, uint8_t opCode, bool end, size_t direct) {
      m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, length);
      m_hdr.m_opCode = opCode;
      m_hdr.m_eof    = end ? 1 : 0;
      m_hdr.m_direct = OCPI_UTRUNCATE(uint8_t, direct);
      put();
    }
    void BasicPort::
    putInternal(size_t length, uint8_t opCode, bool end, size_t direct) {
      if (!m_lastOutBuffer)
	throw OU::Error("put called on output port %s without a previous buffer",
			name().c_str());
      m_lastOutBuffer->send(length, opCode, end, direct);
      ocpiDebug("Putting on %p buffer %p length %zu", this, m_lastOutBuffer, length);
      m_lastOutBuffer = NULL;
    }
    // Step 2: (API/high level) put the next2put buffer, it is full
    void BasicPort::
    put(size_t length, uint8_t opCode, bool end, size_t direct) {
      if (isProvider())
	throw OU::Error("put of output port called on input port %s",
			name().c_str());
      (m_forward ? m_forward : this)->putInternal(length, opCode, end, direct);
    }
    // Step 2: API level put last buffer method on buffer object
    void ExternalBuffer::
    put(size_t length, uint8_t opCode, bool end, size_t direct) {
      m_port.putInternal(length, opCode, end, direct);
    }
    // Step 2: low level API on port
    void BasicPort::
    put() {
      if (m_forward)
	m_forward->put();
      else
	m_next2put->put();
    }
    // Step 2: low level API on buffer
    void ExternalBuffer::
    put() {
      ocpiDebug("Putting port %p buffer %p (fwd %p) length %u", &m_port, this,
		m_port.m_forward, m_hdr.m_length);
      ocpiDebug("Put words %x %x %x %x %x %x",
		((uint32_t*)this)[0],
		((uint32_t*)this)[1],
		((uint32_t*)this)[2],
		((uint32_t*)this)[3],
		((uint32_t*)this)[4],
		((uint32_t*)this)[5]);
      ocpiDebug("Put op %zu eof %zu data %zu direct %zu len %zu full %zu",
		&m_hdr.m_opCode - (uint8_t*)this,
		&m_hdr.m_eof - (uint8_t*)this,
		&m_hdr.m_data - (uint8_t*)this,
		&m_hdr.m_direct - (uint8_t*)this,
		(uint8_t*)&m_hdr.m_length - (uint8_t*)this,
		(uint8_t*)&m_full - (uint8_t*)this);

      m_full = true;
      if (m_next) {
	m_port.m_nWritten++;
	assert(this == m_port.m_next2put);
	m_port.m_next2put = m_next;
      } else if (m_port.m_dtPort) {
	ocpiAssert(m_dtBuffer);
	m_port.m_dtPort->sendOutputBuffer(m_dtBuffer, m_hdr.m_length, m_hdr.m_opCode);
	m_dtBuffer = NULL;
      }
    }

    // Step 2: Standalone EOF.  Returns NULL if it can't go, just like getbuffer.
    bool BasicPort::
    endOfData() {
      if (isProvider())
	throw OU::Error("end of data for output port called on input port %s",
			name().c_str());
      if ((m_forward ? m_forward : this)->m_lastOutBuffer)
	throw OU::Error("end of data called on output port %s with a previous buffer",
			name().c_str());
      ExternalBuffer *b = getEmptyBuffer(); // might forward
      if (b) {
	if (m_dtPort) { // cannot be forwarded
	  ocpiAssert(m_dtLastBuffer.m_dtBuffer);
	  m_dtPort->sendOutputBuffer(m_dtLastBuffer.m_dtBuffer, 0, 0, true, true);
	  m_dtLastBuffer.m_dtBuffer = NULL;
	} else {
	  b->m_hdr.m_length = 0;
	  b->m_hdr.m_opCode = 0;
	  b->m_hdr.m_eof = true;
	  b->m_hdr.m_data = 0; // standalone EOF
	  b->m_full = true;
	  m_nWritten++;
	}
	return true;
      }
      return false;
    }

    void BasicPort::
    put(OCPI::API::ExternalBuffer &buf, size_t len, uint8_t op, bool end, size_t direct) {
      ExternalBuffer &b = static_cast<ExternalBuffer&>(buf);
      b.m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, len);
      b.m_hdr.m_opCode = op;
      b.m_hdr.m_eof = end ? 1 : 0;
      b.m_hdr.m_direct = OCPI_UTRUNCATE(uint8_t, direct);
      put(buf);
    }
    // The zero-copy put of another port's buffer to this port
    void BasicPort::
    put(OA::ExternalBuffer &buf) {
      ExternalBuffer &b = static_cast<ExternalBuffer&>(buf);
      if (m_forward)
	m_forward->put(buf);
      else if (&b.m_port == this)
	buf.put();
      else if (m_next2write) {
	b.m_zcNext = NULL;
	b.m_zcHost = m_next2write;
	(m_next2write->m_zcFront ? m_next2write->m_zcBack->m_zcNext : m_next2write->m_zcFront) =
	  m_next2write->m_zcBack = &b;
      } else if (m_dtPort && b.m_dtBuffer)
	m_dtPort->sendZcopyInputBuffer(*b.m_dtBuffer,
				       b.m_hdr.m_length, b.m_hdr.m_opCode, b.m_hdr.m_eof);
      else
	assert("No support yet for zery-copy send of shim buffer to external port"==0);
    }

    bool BasicPort::
    tryFlush() {
      if (isProvider())
	throw OU::Error("tryflush output port called on input port %s",
			name().c_str());
      if ((m_forward ? m_forward : this)->m_lastOutBuffer)
	throw OU::Error("tryFlush called on output port %s with a previous buffer",
			name().c_str());
      return
	(m_forward ? m_forward->m_nWritten - m_forward->m_nRead : m_nWritten - m_nRead) != 0;
    }

    // Step 3: high level
    OA::ExternalBuffer *BasicPort::
    getBuffer(uint8_t *&data, size_t &length, uint8_t &opCode, bool &end) {
      if (!isProvider())
	throw OU::Error("getBuffer for input port called on output port %s",
			name().c_str());
      if ((m_forward ? m_forward : this)->m_lastInBuffer)
	throw
	  OU::Error("getBuffer called on input port \"%s\" without releasing previous buffer",
		    name().c_str());
      ExternalBuffer *b = getFullBuffer();
      if (b) {
	data = b->data();
	length = b->m_hdr.m_length;
	opCode = b->m_hdr.m_opCode;
	end = b->m_hdr.m_eof;
	(m_forward ? m_forward : this)->m_lastInBuffer = b;
      }
      return b;
    }

    // Step 3: low level
    ExternalBuffer *BasicPort::
    getFullBuffer() {
      if (m_forward)
	return m_forward->getFullBuffer();
      ExternalBuffer *b = m_next2read;
      if (b) { // if shim mode
	if (b->m_full) {
	  m_next2read = b->m_next;
	  ocpiDebug("GetFull on %p returns %p", this, b);
	  return b;
	}
	return NULL;
      }
      size_t length;
      if (m_dtPort &&
	  (m_dtLastBuffer.m_dtBuffer =
	   m_dtPort->getNextFullInputBuffer(m_dtLastBuffer.m_dtData, length,
					    m_dtLastBuffer.m_hdr.m_opCode))) {
	m_dtLastBuffer.m_hdr.m_length = OCPI_UTRUNCATE(uint32_t, length);
	m_dtLastBuffer.m_hdr.m_eof = false;
	return &m_dtLastBuffer;
      }
      return NULL;
    }

    // Step 3: peek at next buffer to read
    bool BasicPort::
    peekOpCode(uint8_t &op) {
      if (m_forward)
	return m_forward->peekOpCode(op);
      if (m_next2read && m_next2read->m_full) {
	op = m_next2read->m_hdr.m_opCode;
	return true;
      }
      return false;
    }

    // This is the lower API
    void BasicPort::
    releaseBuffer() {
      if (m_forward)
	return m_forward->releaseBuffer();
      ExternalBuffer *b = m_next2release;
      if (b) {
	b->m_full = false;
	m_nRead++;
	m_next2release = b->m_next;
	ocpiDebug("Release on %p of %p", this, b);
      } else if (m_dtPort) {
	b = &m_dtLastBuffer;
	assert(m_lastInBuffer == b);
	m_dtPort->releaseInputBuffer(b->m_dtBuffer);
	b->m_dtBuffer = NULL;
      }
      if (m_lastInBuffer == b)
	m_lastInBuffer = NULL;
    }
    // Step 4: release input buffers, return them to empty state
    void BasicPort::
    release() {
      if (!isProvider())
	throw OU::Error("release called on output port %s", name().c_str());
      if (!(m_forward ? m_forward : this)->m_lastInBuffer)
	throw OU::Error("release called on input port \"%s\" without a previous buffer",
			name().c_str());
      return releaseBuffer();
    }

    // Step 4: release, API level on a particular buffer
    void ExternalBuffer::
    release() {
      if (!m_port.m_lastInBuffer)
	throw
	  OU::Error("release called on input port \"%s\" without releasing previous buffer",
		    m_port.name().c_str());
      if (this != m_port.m_lastInBuffer)
	throw
	  OU::Error("release called on input port \"%s\" with the wrong buffer",
		    m_port.name().c_str());
      m_port.releaseBuffer();
    }

    void BasicPort::
    setBufferSize(size_t bufferSize) {
      m_bufferSize = bufferSize;
      getData().data.desc.dataBufferSize = OCPI_UTRUNCATE(uint32_t, m_bufferSize);
    }

    void BasicPort::
    applyConnection(const Launcher::Connection &c) {
      OR::Descriptors &d = getData().data;
      d.role = isProvider() ? c.m_transport.roleIn : c.m_transport.roleOut;
      d.options = isProvider() ? c.m_transport.optionsIn : c.m_transport.optionsOut;
      if (!d.desc.oob.oep[0])
	strcpy(d.desc.oob.oep, c.m_transport.transport.c_str());
      assert(!strncmp(d.desc.oob.oep, c.m_transport.transport.c_str(), strlen(c.m_transport.transport.c_str())));
      setBufferSize(c.m_bufferSize);
    }

    uint8_t *BasicPort::
    allocateBuffers(size_t len) {
      return new uint8_t[len];
    }
    // This is virtual, but during destruction it gets called anyway, so we do the check.
    void BasicPort::
    freeBuffers(uint8_t *p) {
      if (m_allocator == this)
	delete [] p;
    }

    // Make this port into a SHIM - meaning there is no "connection",
    // just the front side (normal API for in-process worker ports)
    // and back side (external port API accessing the same buffers).
    // We do one allocation, where each ExternalBuffer is directly followed by its buffer
    // Alignment and allocation may be specialized
    void BasicPort::
    becomeShim(BasicPort *other) {
      size_t alignment = other ? other->bufferAlignment() : 0;
      alignment = std::max(alignment, bufferAlignment());
      // Align both the headers and the data.
      assert(m_bufferSize != SIZE_MAX);
      m_bufferStride = OU::roundUp(OU::roundUp(sizeof(ExternalBuffer), alignment) +
				   m_bufferSize, alignment);
      if (other && other->hasAllocator() && !hasAllocator()) {
	m_allocator = other;
	other->m_allocator = other;
      } else {
	assert(!other || !other->hasAllocator() ||
	       other->hasAllocator() == hasAllocator());
	m_allocator = this;
      }
      m_allocation = m_allocator->allocateBuffers(m_nBuffers * m_bufferStride);
      uint8_t *p = m_allocation;
      for (unsigned n = 0; n < m_nBuffers; n++, p += m_bufferStride) {
	ExternalBuffer *next =
	  (ExternalBuffer*)(n == m_nBuffers-1 ? m_allocation : p + m_bufferStride);
	new(p) ExternalBuffer(*this, next, n);
      }
      m_next2read = m_next2write = m_next2put = m_next2release = (ExternalBuffer*)m_allocation;
    }

    // Connect inside the same process
    void BasicPort::
    connectInProcess(BasicPort &other) {
      if (&container() != &other.container() ||
	  !container().connectInside(*this, other)) {
	assert(m_bufferSize != SIZE_MAX);
	other.setBufferSize(m_bufferSize);
	becomeShim(&other);
	other.forward2shim(*this);
	portIsConnected();
	other.portIsConnected();
      }
    }

    size_t BasicPort::
    bufferAlignment() const { return OU::BUFFER_ALIGNMENT; }

    void BasicPort::
    forward2shim(BasicPort &shim) {
      shim.m_backward = this;
      m_forward = &shim;
      m_bufferSize = shim.m_bufferSize;
    }

    // Connect inside the same process, but between containers.
    // FIXME: optimization for local connections between ports in the same process,
    // but different containers.
    void BasicPort::
    connectLocal(Launcher::Connection &c) {
      BasicPort
	&in = isProvider() ? *this : *c.m_in.m_port,
	&out = isProvider() ? *c.m_out.m_port : *this;
      bool iDone, oDone;
      OR::Descriptors buf, buf1;
      const OR::Descriptors *result = in.startConnect(NULL, buf, iDone);
      assert(result);
      result = out.startConnect(result, buf1, oDone);
      assert((result && !iDone) || (!result && iDone));
      if (result) {
	result = in.finishConnect(result, buf, iDone);
	assert((result && !oDone) || (!result && oDone));
	if (result)
	  result = out.finishConnect(result, buf1, oDone);
      }
      assert(iDone && oDone && !result);
    }

    // return true if we need more info, false if we're done.
    // Even if we're done, we may have produced info for the other side,
    // and placed it in m_initial.
    bool BasicPort::
    startRemote(Launcher::Connection &c) {
      Launcher::Port
	&p = isProvider() ? c.m_in : c.m_out,
	&other = isProvider() ? c.m_out : c.m_in;
      assert(!p.m_done);
      OR::Descriptors buf, buf1, *otherInfo = NULL;
      if (other.m_initial.length()) {
	ocpiCheck(unpackPortDesc(other.m_initial, buf));
	otherInfo = &buf;
      }
      const OR::Descriptors *result = startConnect(otherInfo, buf1, p.m_done);
      if (result) {
	p.m_started = true;
	packPortDesc(*result, p.m_initial);
      }
      return !p.m_done;
    }

    // This is "try to finish remote".
    // return true if more to do - we need more info
    bool BasicPort::
    finishRemote(Launcher::Connection &c) {
      OR::Descriptors buf, buf1;
      const OR::Descriptors *result;
      Launcher::Port
	&p = isProvider() ? c.m_in : c.m_out,
	&other = isProvider() ? c.m_out : c.m_in;
      assert(!p.m_done);
      ocpiDebug("finishRemote: %p %s i %zu f %zu", this, isProvider() ? "in" : "out",
		other.m_initial.length(), other.m_final.length());
      ocpiCheck(unpackPortDesc(other.m_final.length() ? other.m_final : other.m_initial, buf));
      result = finishConnect(&buf, buf1, p.m_done);
      if (result) {
	packPortDesc(*result, p.m_started ? p.m_final : p.m_initial);
	p.m_started = true;
      }
      ocpiDebug("finishRemote: result %p done %u", result, p.m_done);
      return !p.m_done;
    }

    // Default local behavior for basic ports that need to behave like external or bridge ports
    const OCPI::RDT::Descriptors *BasicPort::
    startConnect(const OCPI::RDT::Descriptors *other, OCPI::RDT::Descriptors &feedback, bool &done) {
      if (isProvider())
	m_dtPort = container().getTransport().createInputPort(getData().data);
      else if (other)
	m_dtPort = container().getTransport().createOutputPort(getData().data, *other);
      if (m_dtPort) {
	// FIXME: put this in the constructor, and have better names
	m_dtPort->setInstanceName(m_metaPort.m_name.c_str());
	if (other)
	  return finishConnect(other, feedback, done);
	done = false;
	return &getData().data;
      }
      done = false;
      return NULL; // we got nuthin
    }

    const OCPI::RDT::Descriptors *BasicPort::
    finishConnect(const OCPI::RDT::Descriptors *other, OCPI::RDT::Descriptors &feedback,
		  bool &done) {
      ocpiDebug("finishConnect enter on '%s' other %p dtport %p",
		name().c_str(), other, m_dtPort);
      const OCPI::RDT::Descriptors *rv;
      if (!m_dtPort) {
	assert(!isProvider());
	assert(other);
	rv = startConnect(other, feedback, done);
	//	if (done) {
	  //	  rv = m_dtPort->finalize(other, getData().data, &feedback, done);
	  //	  assert(!rv);
	//	  assert(done);
	//	}
      } else  
	rv = m_dtPort->finalize(other, getData().data, &feedback, done);
      if (done)
	portIsConnected();
      ocpiDebug("finishConnect exit on '%s' rv %p done %u", name().c_str(), rv, done);
      return rv;
    }

    unsigned BasicPort::fullCount() {
      if (m_forward)
	return m_forward->fullCount();
      if (m_next2read->m_full) {
	unsigned r = m_next2read->m_position, p = m_next2put->m_position;
	return p + (p > r ? 0 : OCPI_UTRUNCATE(unsigned, m_nBuffers)) - r;
      }
      return 0;
    }
    unsigned BasicPort::emptyCount() {
      if (m_forward)
	return m_forward->emptyCount();
      if (!m_next2write->m_full) {
	unsigned w = m_next2write->m_position, r = m_next2release->m_position;
	return r + (r > w ? 0 : OCPI_UTRUNCATE(unsigned, m_nBuffers)) - w;
      }
      return 0;
    }
  } // end of namespace Container
} // end of namespace OCPI
