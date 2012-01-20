/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
 *
 *    Mercury Federal Systems, Incorporated
 *    1901 South Bell Street
 *    Suite 402
 *    Arlington, Virginia 22202
 *    United States of America
 *    Telephone 703-413-0781
 *    FAX 703-413-0784
 *
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


/*
 * Abstact:
 *   Container application context class.
 *
 * Revision History: 
 * 
 *    Author: John F. Miller
 *    Date: 3/2005
 *    Revision Detail: Created
 *
 */

#define WORKER_INTERNAL
#include <OcpiRccPort.h>
#include <OcpiRccContainer.h>
#include <OcpiRccApplication.h>
#include <OcpiRccWorker.h>
#include <OcpiOsMisc.h>
#include <OcpiTransport.h>
#include <OcpiBuffer.h>
#include <OcpiRDTInterface.h>
#include <OcpiOsAssert.h>
#include <OcpiUtilCDR.h>
#include <OcpiPortMetaData.h>
#include <OcpiUtilAutoMutex.h>
#include <OcpiContainerErrorCodes.h>
#include <OcpiContainerMisc.h>
#include <DtTransferInternal.h>
#include <OcpiIntParallelDataDistribution.h>
#include <OcpiPValue.h>
#include <OcpiUtilMisc.h>
#include <OcpiParentChild.h>
#include <OcpiMetadataWorker.h>

namespace OM = OCPI::Metadata;
namespace OC = OCPI::Container;
namespace OS = OCPI::OS;
namespace OU = OCPI::Util;
namespace OA = OCPI::API;

namespace OCPI {
  namespace RCC {
#if 0
#define SET_LAST_ERROR_TO_WORKER_ERROR_STRING(x)                        \
  if ( x->m_rcc_worker->m_context->errorString ) {                                \
    x->m_lastError=x->m_rcc_worker->m_context->errorString;                        \
  }                                                                        \
  else {                                                                \
    x->m_lastError = "Worked returned failure, but did not set errorString"; \
  }                                                                        \
  x->m_rcc_worker->m_context->errorString=NULL;

#define SET_LAST_ERROR_UNKNOWN_WORKER(x) x->m_lastError = "Unknown Worker Type"
#define SET_LAST_ERROR_WORKER_IN_UNUSABLE_STATE(x) x->m_lastError = "Worker in Unusable State"
#define SET_LAST_ERROR_INVALID_SEQUENCE(x) x->m_lastError = "Invalid Control Sequence"
#define SET_LAST_ERROR_ALL_REQUIRED_PORTS_NOT_CONNECTED(x) x->m_lastError = "Not all required ports are connected"
#define SET_LAST_ERROR_PORT_COUNT_MISMATCH(x) x->m_lastError = "Input/Output port count mismatch between worker and meta-data"
#define SET_LAST_ERROR_TO_TEST_NOT_IMPLEMENTED(x) x->m_lastError = "Test Not Implemented"
#define SET_LAST_ERROR_TO_PROPTERY_OVERRUN(x) x->m_lastError = "Property Overrun error"
#endif

#if 0
void 
Worker::
overRidePortInfo( OM::Port & portData )
{
  RCCPortInfo* pi = NULL;
  if ( m_dispatch->portInfo == NULL ) {
    return;
  }
  int n=0;
  while ( m_dispatch->portInfo[n].port != RCC_NO_ORDINAL ) {
    if ( m_dispatch->portInfo[n].port == portData.ordinal  ) {
      pi = &m_dispatch->portInfo[n];
      break;
    }
    n++;
  }
  if ( pi ) {
#ifndef NDEBUG
    printf("\nWorker Port info is non NULL, overriding port defaults from s,bc -> %d,%d to %d,%d\n",
           portData.minBufferCount, portData.minBufferSize, pi->minBuffers, pi->maxLength );
#endif
    portData.minBufferCount = ( portData.minBufferCount > pi->minBuffers ) ? portData.minBufferCount : pi->minBuffers;
    portData.minBufferSize  = portData.maxBufferSize  = pi->maxLength;

  }
}
#endif

Worker::
Worker( Application & app, Artifact *art, const char *name,
	ezxml_t impl, ezxml_t inst, const OU::PValue *wParams)
  : OC::WorkerBase<Application,Worker,Port>(app, art, name, impl, inst, wParams),
    // Note the "hack" to use "name" as dispatch when artifact is not present
    m_context(0), m_mutex(app.container()), enabled(false),sourcePortCount(0),targetPortCount(0),
    m_nPorts(0),//sourcePorts(1), targetPorts(1),
    runConditionSS(0), worker_run_count(0),
    m_transport(app.parent().getTransport())
{
  if (art)
    m_dispatch = art->getDispatch(ezxml_cattr(impl, "name"));
  else
    m_dispatch = (RCCDispatch *)name;

  initializeContext();
  // If we have an event handler, we need to inform it about the timeout
  if (  m_dispatch->runCondition && m_dispatch->runCondition->timeout ) {
    runTimeout.set(m_dispatch->runCondition->usecs / 1000000,
		   (m_dispatch->runCondition->usecs % 1000000) * 1000);
    if ( m_transport.m_transportGlobal->getEventManager() ) {

#ifdef EM_PORT_COMPLETE
      parent().myparent->m_transport->m_transportGlobal->getEventManager()->setMinTimeout( workerId, 
                                                                        wd->runCondition->usecs );
#endif

    }
  }
}

void 
Worker::
read(uint32_t offset, 
     uint32_t nBytes, 
     void * p_data  )
{
  OU::AutoMutex guard(m_mutex);
  if ( (offset+nBytes) > m_dispatch->propertySize ) {
    throw OU::EmbeddedException( OU::PROPERTY_GET_EXCEPTION, NULL, OU::ApplicationRecoverable);
  }
  memcpy( p_data, (char*)m_context->properties+offset, nBytes );
}

void 
Worker::
write(uint32_t offset, 
      uint32_t nBytes, 
      const void * p_data  )
{
  OU::AutoMutex guard (m_mutex);
  if ( (offset+nBytes) > m_dispatch->propertySize ) {
    throw OU::EmbeddedException( OU::PROPERTY_SET_EXCEPTION, NULL, OU::ApplicationRecoverable);
  }
  memcpy( (char*)m_context->properties+offset, p_data, nBytes );
}

Worker::
~Worker()
{
  // FIXME - this sort of thing should be generic
  if (enabled) {
    enabled = false;
    controlOperation(OM::Worker::OpStop);
    controlOperation(OM::Worker::OpRelease);
  }
#ifdef EM_PORT_COMPLETE
    RCCDispatch* wd = m_dispatch;
    // If we have an event handler, we need to inform it about the timeout
    if ( wd->runCondition && wd->runCondition->timeout ) {
      if ( parent().m_transport->m_transportGlobal->getEventManager() ) {
        parent().m_transport->m_transportGlobal->getEventManager()->removeMinTimeout( w->workerId );
      }
    }
#endif

  deleteChildren();
  OS::uint32_t m = 0;
  while ( m_context->memories && m_context->memories[m] ) {
    delete [] (char*)m_context->memories[m];
    m++;
  }
  delete[] m_context->memories;
  delete[] m_context->runCondition->portMasks;
  delete m_context->runCondition;
  delete[] (char*)m_context->properties;
  delete[] m_context;
}

static void
  rccRelease(RCCBuffer *),
  rccSend(RCCPort *, RCCBuffer*, RCCOrdinal op, uint32_t length),
  rccTake(RCCPort *,RCCBuffer *old_buffer, RCCBuffer *new_buffer);
static RCCBoolean
  rccRequest(RCCPort *port, uint32_t maxlength),
  rccAdvance(RCCPort *port, uint32_t maxlength),
  rccWait(RCCPort *, uint32_t max, uint32_t usecs);

static void rccRelease( RCCBuffer* buffer )
{
  OCPI::DataTransport::BufferUserFacet* dti_buffer = buffer->containerBuffer;
  ocpiAssert(dti_buffer);
  Port * port = static_cast<Port*>( dti_buffer->m_ud );
  ocpiAssert(port);
  // An API might be called incorrectly.
  if (port->isOutput())
    throw OU::Error("RCC release container function called on an output port, which is not supported");
  port->release(dti_buffer);    
}
static void rccSend( ::RCCPort* rccPort, ::RCCBuffer* rccBuffer, ::RCCOrdinal op, ::uint32_t len )
{
  Port* port = rccPort->containerPort;
  ocpiAssert(port);
  if ( !port->isOutput() )
    throw OU::Error("The 'send' container function cannot be called on an input port");
  OCPI::DataTransport::BufferUserFacet *buffer = rccBuffer->containerBuffer;
  Port *bufferPort = static_cast<Port*>(buffer->m_ud);
  if (bufferPort != port && bufferPort->isOutput())
    throw OU::Error("Cannot send a buffer from a different output port");
  port->send(buffer, len, op);
}
static RCCBoolean rccAdvance( ::RCCPort* rccPort, ::uint32_t max )
{
  Port *port = rccPort->containerPort; 
  ocpiAssert(port);
  bool ready = port->advance();
  if (ready && max && max > rccPort->current.maxLength)
    throw OU::Error("Output buffer request/advance (size %u) greater than buffer size (%u)",
		    max, rccPort->current.maxLength);
  return ready;
}

static RCCBoolean rccRequest( ::RCCPort* rccPort, ::uint32_t max )
{
  if (rccPort->current.data )
    return true;
  Port* port = rccPort->containerPort;
  ocpiAssert(port);
  bool ready = port->request();
  if (ready && max && port->isOutput() && max < rccPort->output.length)
    throw OU::Error("Requested output buffer size is unavailable");
  return ready;
}

static RCCBoolean rccWait( ::RCCPort* port, ::uint32_t max, ::uint32_t usec )
{
  // Not implemented yet
  ( void ) port;
  ( void ) max;
  ( void ) usec;
  return false;
}
static void rccTake(RCCPort *rccPort, RCCBuffer *oldBuffer, RCCBuffer *newBuffer)
{
  Port *port = rccPort->containerPort;
  if ( port->isOutput() )
    throw OU::Error("The 'take' container function cannot be used on an output port");
  if (!rccPort->current.data)
    throw OU::Error("The 'take' container function cannot be called when there is no current buffer");
  port->take(oldBuffer, newBuffer);
}

// FIXME:  recover memory on exceptions
void Worker::
initializeContext()
{        
  RCCDispatch *wd = m_dispatch;
  // Create our memory spaces
  int idx=0;
  while( wd->memSizes && wd->memSizes[idx] ) {
#ifndef NDEBUG
    printf("Allocating %d bytes of data for worker memory\n", wd->memSizes[idx] );
#endif
    idx++;
  }
  int mcount = idx;

  // check masks for bad bits
  m_nPorts = getNumPorts();
  if (m_nPorts) {
    if (m_nPorts != wd->numInputs + wd->numOutputs)
      throw OU::Error("metadata port count (%u) and dispatch port count (in: %u + out: %u) differ",
		      m_nPorts, wd->numInputs, wd->numOutputs);
  } else if (wd->numInputs || wd->numOutputs)
    m_nPorts = wd->numInputs + wd->numOutputs;
  int32_t mask = -1 << m_nPorts;
  if (mask & wd->optionallyConnectedPorts)
    throw OU::EmbeddedException( OU::PORT_COUNT_MISMATCH,
				 "optional port mask is invalid",
				 OU::ApplicationRecoverable);
  unsigned rc_count = 0;
  if ( wd->runCondition && wd->runCondition->portMasks )
    while (wd->runCondition->portMasks[rc_count] ) {
      if (mask & wd->runCondition->portMasks[rc_count])
	throw OU::EmbeddedException( OU::PORT_COUNT_MISMATCH,
				     "run condition mask is invalid",
				     OU::ApplicationRecoverable);
      rc_count++;
    }

  // Create our context
  m_context = (RCCWorker *)new char[sizeof(RCCWorker) + m_nPorts*sizeof(RCCPort)];
  m_context->properties = 0;
  static RCCContainer rccContainer = { rccRelease, rccSend, rccRequest, rccAdvance, rccWait, rccTake};
  m_context->container = rccContainer;
  if ( mcount ) {
    try {
      m_context->memories = new void*[mcount+1];
    }
    catch( std::bad_alloc ) {
      delete[] m_context;
      throw OU::EmbeddedException( OU::NO_MORE_MEMORY, "worker requested too much memory", OU::ApplicationRecoverable);
    }
    m_context->memories[mcount] = NULL;
  }
  else {
    m_context->memories = NULL;
  }
  
  m_context->connectedPorts = 0;
  m_context->runCondition = new RCCRunCondition;
 
  if ( wd->runCondition ) {
    if ( wd->runCondition->portMasks ) {
      // Add one for the zero based terminator
      rc_count++;

      m_context->runCondition->portMasks = new RCCPortMask[rc_count];
      memcpy( m_context->runCondition->portMasks, wd->runCondition->portMasks, sizeof(RCCPortMask)*rc_count);
    }  
    else { // portMask is NULL, per CP289 spec, timeout only run condition
      m_context->runCondition->portMasks = new RCCPortMask[1];
      m_context->runCondition->portMasks[0] = 0;
    }
    
  }
  else {
    rc_count = 1;
    m_context->runCondition->portMasks = new RCCPortMask[2];
    m_context->runCondition->portMasks[0] = 0;
    m_context->runCondition->portMasks[1] = 0;

    for ( int n=0; n<(wd->numInputs+wd->numOutputs); n++ ) {
      m_context->runCondition->portMasks[0] |=  (1<<n);
    }
  }

  // This is the superset of the run conditions
  for (unsigned n=0; n< rc_count; n++ )
    runConditionSS |= m_context->runCondition->portMasks[n];

  // We fill in one of these structures for all of the ports that are defined in the worker.  However,
  // the actual data ports may be optional at runtime.
  for (unsigned n=0; n<m_nPorts; n++ ) {
    m_context->ports[n].containerPort = NULL;
    m_context->ports[n].current.data = NULL;
    m_context->ports[n].current.maxLength = 0;
    m_context->ports[n].callBack = 0;
  }

  idx=0;
  while( wd->memSizes && wd->memSizes[idx] ) {
    try {
      m_context->memories[idx] = new char*[wd->memSizes[idx]];
      memset(m_context->memories[idx], 0, wd->memSizes[idx]);
    }
    catch( std::bad_alloc ) {
      delete[] m_context;
      throw OU::EmbeddedException( OU::NO_MORE_MEMORY, "worker requested too much memory", OU::ApplicationRecoverable);
    }
    idx++;
  }
  
  // Now create and initialize the worker properties
  m_context->properties = new char[wd->propertySize + 4];
  memset(m_context->properties, 0, sizeof(char)*wd->propertySize);
        
}

// Called from the generic getPort when the port is not found.
// Also called from createInputPort and createOutputPort locally
OC::Port & 
Worker::
createPort(const OCPI::Metadata::Port& mp, const OCPI::Util::PValue *params)
{
  TRACE(" OCPI::RCC::Worker::createPort()");
  if ( mp.minBufferCount == 0)
    throw OU::EmbeddedException( OU::BAD_PORT_CONFIGURATION, "0 buffer count",
				 OU::ApplicationRecoverable);
  OU::AutoMutex guard (m_mutex);
  if (mp.ordinal >= m_dispatch->numInputs + m_dispatch->numOutputs)
    throw OU::EmbeddedException(OU::PORT_NOT_FOUND,
				"Port id exceeds port count", OU::ApplicationFatal);
  if (mp.ordinal > 32)
    throw OU::EmbeddedException(OU::PORT_NOT_FOUND,
				"Port id exceeds max port count of 32", OU::ApplicationFatal);
  // If the worker binary has port info, check it against the metadata for consistency
  if (m_dispatch->portInfo)
    for (RCCPortInfo* pi = m_dispatch->portInfo; pi->port != RCC_NO_ORDINAL; pi++)
      if (pi->port == mp.ordinal) {
#ifndef NDEBUG
        printf("\nWorker PortInfo for port %d, bc,s: %d,%d, metadata is %d,%d\n",
	       mp.ordinal, pi->minBuffers, pi->maxLength, mp.minBufferCount, mp.m_minBufferSize);
#endif
	if (pi->minBuffers > mp.minBufferCount) // bad: worker needs more than metadata   
	  throw OU::EmbeddedException(OU::PORT_NOT_FOUND,
				      "Worker metadata inconsistent with RCC PortInfo",
				      OU::ContainerFatal);
      }
  if (mp.provider) { // input
    if (++targetPortCount > m_dispatch->numInputs)
      throw OU::EmbeddedException(OU::PORT_NOT_FOUND,
				  "Target Port count exceeds configuration", OU::ApplicationFatal);
  } else if (++sourcePortCount > m_dispatch->numOutputs )
    throw OU::EmbeddedException(OU::PORT_NOT_FOUND,
				"Source Port count exceeds configuration", OU::ApplicationFatal);
  Port *port;
  try {
    port = new Port(*this, mp, params, &m_context->ports[mp.ordinal]);
  }
  catch(std::bad_alloc) {
    throw OU::EmbeddedException( OU::NO_MORE_MEMORY, "new", OU::ContainerFatal);
  }
  // We know the metadata is more contrained than port info
  // FIXME: RccPort Object should know about its C RCCPort and do this itself
  // FIXME: this can change on connections
  m_context->ports[mp.ordinal].current.maxLength = port->getData().data.desc.dataBufferSize;
  m_context->ports[mp.ordinal].containerPort = port;

#if 0
  if (mp.provider) { // input
    targetPorts.insertToPosition(port, mp.ordinal);    
  } else            // output
    // Defer the real work until the port is connected.
    sourcePorts.insertToPosition(port, mp.ordinal);
#endif

  return *port;
}


// Common code for the test API to create ports by ordinal
// We add the explicitly specified buffer count and size to the
// list of properties.
static  OC::Port &
createTestPort( Worker *w, OM::PortOrdinal portId,
                OS::uint32_t bufferCount,
                OS::uint32_t bufferSize, 
		bool isProvider,
		const OU::PValue* props) {
  // Add runtime properties if buffer count and size are being adjusted
  unsigned n = props->length();
  OA::PVarray * p = new OA::PVarray(n + 3);
  OA::PVarray &myProps = *p;
  unsigned i;
  for (i = 0; i < n; i++)
    myProps[i] = props[i];
  if (bufferCount)
    myProps[i++] = OA::PVULong("bufferCount", bufferCount);
  if (bufferSize)
    myProps[i++] = OA::PVULong("bufferSize", bufferSize);
  myProps[i] = OA::PVEnd;

  OM::Port *pmd = new OM::Port;;
  pmd->name = isProvider ? "unnamed input" : "unnamed output";
  pmd->ordinal = portId;
  pmd->provider = isProvider;
  pmd->bufferSize = bufferSize;
 // FIXME: stash the params and the metadata for destruction to avoid a leak.
  return w->createPort(*pmd, myProps);
}
 OC::Port &
Worker::
createOutputPort( 
                 OM::PortOrdinal     portId,
                 OS::uint32_t    bufferCount,
                 OS::uint32_t    bufferSize, 
                 const OU::PValue*              props            
                 )
  throw ( OU::EmbeddedException )
{
  TRACE(" OCPI::RCC::Container::createOutputPort()");

  return createTestPort(this, portId, bufferCount, bufferSize, false, props);
}




OC::Port &
Worker::
createInputPort( 
		OM::PortOrdinal    portId,   
                 OS::uint32_t   bufferCount,
                 OS::uint32_t   bufferSize, 
                 const OU::PValue*             props            
                 )
  throw ( OU::EmbeddedException )
{
  TRACE("OCPI::RCC::Container::createInputPort()");

  return createTestPort(this, portId, bufferCount, bufferSize, true, props);
}


void 
Worker::
portIsConnected( unsigned ordinal )
{
  m_context->connectedPorts |= (1<<ordinal);
}

void 
Worker::
prepareProperty(OM::Property& md , 
		volatile void *&writeVaddr,
		const volatile void *&readVaddr)
{
  (void)readVaddr;
  if (md.m_baseType != OA::OCPI_Struct && !md.m_isSequence && md.m_baseType != OA::OCPI_String &&
      OU::baseTypeSizes[md.m_baseType] <= 32 &&
      !md.m_writeError) {
    if ( (md.m_offset+sizeof(md.m_baseType)) > m_dispatch->propertySize ) {
      throw OU::EmbeddedException( OU::PROPERTY_SET_EXCEPTION, NULL, OU::ApplicationRecoverable);
    }
    writeVaddr = (uint8_t*)m_context->properties + md.m_offset;
  }
}

RCCPortMask Worker::getReadyPorts() {
  // Create the active mask
  RCCPortMask readyMask = 0;
  RCCPort *rccPort = m_context->ports;
  RCCPortMask portMask = 1;
  for (unsigned n = 0; n < m_nPorts; n++, rccPort++, portMask <<= 1)
    if ((m_context->connectedPorts & portMask & runConditionSS) &&
	rccPort->containerPort &&
	rccPort->containerPort->checkReady())
      readyMask |= portMask;
  return readyMask;
}

extern volatile int ocpi_dbg_run;
void Worker::run(bool &anyone_run) {
  OU::AutoMutex guard (mutex(), true);
  
  if (!enabled)
    return;
  bool run_condition_met = false;
  RCCBoolean timeout = false;
  RCCPortMask readyMask = 0;

  // Break from this "do" when we know whether we are running or not
  do {
    // No run condition at all means run
    if (!m_context->runCondition) {
      run_condition_met = true;
      break;
    }
    // Check if this worker has a timer for a run condition
    if (m_dispatch->runCondition && m_dispatch->runCondition->timeout ) {
      OS::ElapsedTime et;
      runTimer.stop();
      runTimer.getValue( et );
      runTimer.start();
      if ( et > runTimeout ) {          
#ifndef NDEBUG
	printf("WORKER TIMED OUT, timer time = %d,%d -- run timer = %d,%d\n", 
	       et.seconds(), et.nanoseconds(), runTimeout.seconds(), runTimeout.nanoseconds() );
#endif
	run_condition_met = true;
	timeout = true;
	break;
      }
    }
    // If no port masks, then we won't run
    if (!m_context->runCondition->portMasks ||
	!m_context->runCondition->portMasks[0])
      break;
    // Ok, do the work to find out which ports are ready
    readyMask = getReadyPorts();
#ifndef NDEBUG
    if ( ocpi_dbg_run ) {
      printf("WORKER RUN: worker ready mask = %d\n", readyMask );
    }
#endif
    // We need to ignore optional ports that are NOT connected,
    // So we make believe they are ready
    readyMask |=
      m_dispatch->optionallyConnectedPorts &
      ~m_context->connectedPorts;
    if (!readyMask)
      break;
    // See if any of our masks are satisfied
    for (::RCCPortMask *pmp = m_context->runCondition->portMasks;
	 *pmp; pmp++)
      if ((*pmp & readyMask) == *pmp) {
	run_condition_met = true;
	break;
      }

  } while (0);
  assert(enabled);
  if (run_condition_met) {

    // If the worker has defined a port method, we will call it instead of the run method
    // for each port that has one [FIXME should these just run if the port is ready in any case?]
    OM::PortOrdinal pord = 0;
    bool execute_run = true;
    for (uint32_t mtest = 1; mtest < readyMask; mtest <<= 1, pord++) {
      if (mtest & readyMask &&
	  m_context->ports[pord].callBack ) {
	execute_run = false;
	if ( m_context->ports[pord].callBack(m_context,
					     &m_context->ports[pord], 
					     RCC_OK) != RCC_OK) {
	  enabled = false;
	  // FIXME: do anything with timers?  release?
	  setControlState(OC::UNUSABLE);
	}
      }
    }

    assert(enabled);
    if (m_dispatch->run && execute_run ) {
      anyone_run = true;
      OCPI_EMIT_("End Worker Evaluation");
      RCCBoolean newRunCondition = false;
      // FIXME: implement new runcondition!!!
      switch (m_dispatch->run(m_context, timeout, &newRunCondition)) {
      case RCC_ADVANCE:
	advanceAll();
	break;
      case RCC_ADVANCE_DONE:
	advanceAll();
      case RCC_DONE:
	// FIXME:  release all current buffers
	enabled = false;
	setControlState(OC::FINISHED);
	break;
      case RCC_OK:
	assert(enabled);
	runTimer.stop();
	runTimer.reset();
	runTimer.start();
	break;
      default:
	enabled = false;
	setControlState(OC::UNUSABLE);
	runTimer.stop();
      }
      checkDeadLock();
      worker_run_count++;
    }
  }
}
     void Worker::checkDeadLock() {}
     void Worker::advanceAll() {
       OCPI_EMIT_( "Start Advance All" );
       RCCPort *rccPort = m_context->ports;
       for (unsigned n = 0; n < m_nPorts; n++, rccPort++)
	 if (rccPort->current.data)
	   rccPort->containerPort->advance();
       OCPI_EMIT_( "End Advance All" );
     }

// Note we are already under a mutex here
void Worker::controlOperation(OM::Worker::ControlOperation op) {
  RCCResult rc = RCC_OK;
  OU::AutoMutex guard (mutex(), true);
  switch (op) {
  case OM::Worker::OpInitialize:
    if (m_dispatch->initialize)
      rc = m_dispatch->initialize(m_context);
    break;
  case OM::Worker::OpStart:
    // FIXME: ports that are not connected are not "created".
    // If a worker gets started before all of its ports are created: error
    if ( (int)(targetPortCount + sourcePortCount) != 
	 (int)(m_dispatch->numInputs + m_dispatch->numOutputs ) )
      throw OU::EmbeddedException( OU::PORT_COUNT_MISMATCH,
				   "Port count different than metadata",
				   OU::ApplicationRecoverable);
    // If the worker does not have all the required ports connected: error
    for (Port *port = firstChild(); port; port = port->nextChild())
      if (!(1<<port->portOrdinal() & m_dispatch->optionallyConnectedPorts)) {

	if ( ! port->definitionComplete() )
	  throw OU::EmbeddedException( OU::PORT_NOT_CONNECTED, NULL, OU::ApplicationRecoverable);	

      }
    if (!m_dispatch->start ||
	(rc = m_dispatch->start(m_context)) == RCC_OK) {
      enabled = true;
      runTimer.start();// FIXME: this this right for re-start too?
    }
    break;
  case OM::Worker::OpStop:
    if (enabled) {
      enabled = false;
      runTimer.stop();
      runTimer.reset();
    }
    if (m_dispatch->stop)
      rc = m_dispatch->stop(m_context);
    break;
    // like stop, except don't call stop
  case OM::Worker::OpRelease:
    if (enabled) {
      enabled = false;
      runTimer.stop();
      runTimer.reset();
    }
    if (m_dispatch->release)
      rc = m_dispatch->release(m_context);
    break;
  case OM::Worker::OpTest:
    if (m_dispatch->test)
      rc = m_dispatch->test(m_context);
    else
      throw OU::EmbeddedException( OU::TEST_NOT_IMPLEMENTED,
				   "Worker has no test implementation",
				   OU::ApplicationRecoverable);
    break;
  case OM::Worker::OpBeforeQuery:
    if (m_dispatch->beforeQuery)
      rc = m_dispatch->beforeQuery(m_context);
    break;
  case OM::Worker::OpAfterConfigure:
    if (m_dispatch->afterConfigure)
      rc = m_dispatch->afterConfigure(m_context);
  case OM::Worker::OpsLimit:
    break;
  }
  switch (rc) {
  case RCC_OK:
    break;
  case RCC_ERROR:
    throw OU::EmbeddedException( OU::WORKER_ERROR, m_context->errorString,
				 OU::ApplicationRecoverable);
    break;
  case RCC_FATAL:
    enabled = false;
    setControlState(OC::UNUSABLE);
    throw OU::EmbeddedException( OU::WORKER_FATAL, m_context->errorString,
				 OU::ApplicationFatal);
    break;
  default:
    enabled = false;
    throw OU::EmbeddedException( OU::WORKER_API_ERROR, "Control operation returned invalid RCCResult",
				 OU::ApplicationFatal);
  }    
}

      // These property access methods are called when the fast path
      // is not enabled, either due to no MMIO or that the property can
      // return errors. 
#undef OCPI_DATA_TYPE_S
      // Set a scalar property value
#define OCPI_DATA_TYPE(sca,corba,letter,bits,run,pretty,store)                  \
  void Worker::set##pretty##Property(const OA::Property &p, const run val) const { \
    assert(p.m_info.m_baseType == OCPI::API::OCPI_##pretty);		        \
        if (p.m_info.m_writeError)                                              \
          throw; /*"worker has errors before write */                           \
        store *pp = (store *)(getPropertyVaddr() + p.m_info.m_offset);	        \
        if (bits > 32) {                                                        \
          assert(bits == 64);                                                   \
          uint32_t *p32 = (uint32_t *)pp;                                       \
          p32[1] = ((const uint32_t *)&val)[1];                                 \
          p32[0] = ((const uint32_t *)&val)[0];                                 \
        } else                                                                  \
          *pp = *(const store *)&val;                                           \
        if (p.m_info.m_writeError)					        \
          throw; /*"worker has errors after write */                            \
      }                                                                         \
  void Worker::set##pretty##SequenceProperty(const OA::Property &p,const run *vals, \
					 unsigned length) const {		\
        if (p.m_info.m_writeError)                                              \
          throw; /*"worker has errors before write */                           \
        memcpy((void *)(getPropertyVaddr() + p.m_info.m_offset + p.m_info.m_align), vals, \
	       length * sizeof(run));					        \
        *(uint32_t *)(getPropertyVaddr() + p.m_info.m_offset) = length;         \
        if (p.m_info.m_writeError)                                              \
          throw; /*"worker has errors after write */                            \
      }
      // Set a string property value
      // ASSUMPTION:  strings always occupy at least 4 bytes, and
      // are aligned on 4 byte boundaries.  The offset calculations
      // and structure padding are assumed to do this.
#define OCPI_DATA_TYPE_S(sca,corba,letter,bits,run,pretty,store)                   \
  void Worker::set##pretty##Property(const OA::Property &p, const run val) const { \
        unsigned ocpi_length;                                                      \
        if (!val || (ocpi_length = strlen(val)) > p.m_info.m_stringLength)         \
          throw; /*"string property too long"*/;                                   \
        if (p.m_info.m_writeError)                                                 \
          throw; /*"worker has errors before write */                              \
        uint32_t *p32 = (uint32_t *)(getPropertyVaddr() + p.m_info.m_offset);      \
        /* if length to be written is more than 32 bits */                         \
        if (++ocpi_length > 32/CHAR_BIT)                                           \
          memcpy(p32 + 1, val + 32/CHAR_BIT, ocpi_length - 32/CHAR_BIT);           \
        uint32_t i;                                                                \
        memcpy(&i, val, 32/CHAR_BIT);                                              \
        p32[0] = i;                                                                \
        if (p.m_info.m_writeError)                                                 \
          throw; /*"worker has errors after write */                               \
      }                                                                            \
  void Worker::set##pretty##SequenceProperty(const OA::Property &p,const run *vals,\
					 unsigned length) const {		   \
        if (p.m_info.m_writeError)                                                 \
          throw; /*"worker has errors before write */                              \
        char *cp = (char *)(getPropertyVaddr() + p.m_info.m_offset + 32/CHAR_BIT); \
        for (unsigned i = 0; i < length; i++) {                                    \
          unsigned len = strlen(vals[i]);                                          \
          if (len > p.m_info.m_stringLength)	                                   \
            throw; /* "string in sequence too long" */                             \
          memcpy(cp, vals[i], len+1);                                              \
        }                                                                          \
        *(uint32_t *)(getPropertyVaddr() + p.m_info.m_offset) = length;            \
        if (p.m_info.m_writeError)                                                 \
          throw; /*"worker has errors after write */                               \
      }
      OCPI_PROPERTY_DATA_TYPES
#undef OCPI_DATA_TYPE_S
#undef OCPI_DATA_TYPE
      // Get Scalar Property
#define OCPI_DATA_TYPE(sca,corba,letter,bits,run,pretty,store)		    \
      run Worker::get##pretty##Property(const OA::Property &p) const {      \
        if (p.m_info.m_readError )					    \
          throw; /*"worker has errors before read "*/			    \
        uint32_t *pp = (uint32_t *)(getPropertyVaddr() + p.m_info.m_offset);\
        union {								    \
	  run r;							    \
	  uint32_t u32[bits/32];                                            \
        } u;								    \
        if (bits > 32)							    \
          u.u32[1] = pp[1];						    \
        u.u32[0] = pp[0];						    \
        if (p.m_info.m_readError )					    \
          throw; /*"worker has errors after read */			    \
        return u.r;							    \
      }									    \
      unsigned Worker::get##pretty##SequenceProperty(const OA::Property &p, \
					     run *vals,			    \
					     unsigned length) const {	    \
        if (p.m_info.m_readError )					    \
          throw; /*"worker has errors before read "*/			    \
        uint32_t n = *(uint32_t *)(getPropertyVaddr() + p.m_info.m_offset); \
        if (n > length)							    \
          throw; /* sequence longer than provided buffer */		    \
        memcpy(vals,							    \
	       (void*)(getPropertyVaddr() + p.m_info.m_offset + p.m_info.m_align), \
               n * sizeof(run));                                            \
        if (p.m_info.m_readError )					    \
          throw; /*"worker has errors after read */			    \
        return n;							    \
      }

      // ASSUMPTION:  strings always occupy at least 4 bytes, and
      // are aligned on 4 byte boundaries.  The offset calculations
      // and structure padding are assumed to do this.
#define OCPI_DATA_TYPE_S(sca,corba,letter,bits,run,pretty,store)	    \
      void Worker::get##pretty##Property(const OA::Property &p, char *cp,   \
					   unsigned length) const {	    \
	  unsigned stringLength = p.m_info.m_stringLength;                  \
	  if (length < stringLength+1)			                    \
	    throw; /*"string buffer smaller than property"*/;		    \
	  if (p.m_info.m_readError)					    \
	    throw; /*"worker has errors before write */			    \
	  uint32_t i32, *p32 = (uint32_t *)(getPropertyVaddr() + p.m_info.m_offset);   \
	  memcpy(cp + 32/CHAR_BIT, p32 + 1, stringLength + 1 - 32/CHAR_BIT);\
	  i32 = *p32;							    \
	  memcpy(cp, &i32, 32/CHAR_BIT);				    \
	  if (p.m_info.m_readError)					    \
	    throw; /*"worker has errors after write */			    \
	}								    \
      unsigned Worker::get##pretty##SequenceProperty			    \
	(const OA::Property &p, char **vals, unsigned length, char *buf,    \
	 unsigned space) const {					    \
        if (p.m_info.m_readError)					    \
          throw; /*"worker has errors before read */                        \
        uint32_t                                                            \
          n = *(uint32_t *)(getPropertyVaddr() + p.m_info.m_offset),        \
          wlen = p.m_info.m_stringLength + 1;                               \
        if (n > length)                                                     \
          throw; /* sequence longer than provided buffer */                 \
        char *cp = (char *)(getPropertyVaddr() + p.m_info.m_offset + 32/CHAR_BIT); \
        for (unsigned i = 0; i < n; i++) {                                  \
          if (space < wlen)                                                 \
            throw;                                                          \
          memcpy(buf, cp, wlen);                                            \
          cp += wlen;                                                       \
          vals[i] = buf;                                                    \
          unsigned slen = strlen(buf) + 1;                                  \
          buf += slen;                                                      \
          space -= slen;                                                    \
        }                                                                   \
        if (p.m_info.m_readError)                                           \
          throw; /*"worker has errors after read */                         \
        return n;                                                           \
      }
      OCPI_PROPERTY_DATA_TYPES
#undef OCPI_DATA_TYPE_S
#undef OCPI_DATA_TYPE
#define OCPI_DATA_TYPE_S OCPI_DATA_TYPE

}
}
