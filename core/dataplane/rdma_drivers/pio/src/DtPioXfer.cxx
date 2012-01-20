
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
 *   This file contains the implementation for the base class for SMB transfers.
 *
 *  John Miller - 6/15/09
 *  Fixed Coverity issues
 *
 *  John Miller -  7/20/04
 *  Initial version
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <DtSharedMemoryInternal.h>
#include <DtPioXfer.h>
#include <xfer_if.h>
#include <OcpiList.h>
#include <OcpiUtilHash.h>
#include <OcpiOsMisc.h>
#include <OcpiOsAssert.h>
#include <OcpiUtilAutoMutex.h>
#include <DtExceptions.h>
#include <OcpiPValue.h>

namespace DataTransfer {
using namespace OCPI::Util;
using namespace OCPI::OS;


const char *pio = "pio"; // name passed to inherited template class
PIOXferFactory::PIOXferFactory()
  throw ()
 {
  printf("In PIOXferFactory::PIOXferFactory()\n");

  // Empty
}

// Destructor
PIOXferFactory::~PIOXferFactory()
  throw ()
{
  //  clearCache();
}

#if 0
/***************************************
 *  This method is used to flush any cached items in the factoy
 ***************************************/
void PIOXferFactory::clearCache()
{
  GppEndPoint *loc;
  for ( OCPI::OS::uint32_t n=0; n<g_locations.getElementCount(); n++ ) {
    loc = static_cast<GppEndPoint*>(g_locations.getEntry(n));
    delete loc;
  }
  g_locations.destroyList();
}


// Get the location via the endpoint
EndPoint* PIOXferFactory::getEndPoint( std::string& end_point, bool /* local */ )
{ 
  OCPI::Util::SelfAutoMutex guard (this); 

  GppEndPoint *loc;
  for ( OCPI::OS::uint32_t n=0; n<g_locations.getElementCount(); n++ ) {
    loc = static_cast<GppEndPoint*>(g_locations.getEntry(n));
    if ( end_point == loc->end_point ) {
      return loc;
    }
  }

  loc = new GppEndPoint(end_point);
        
  // This is a test case to make sure that a factory can modify the endpoint
  // string and system can handle it !!

  g_locations.insert( loc );
  return loc;
}
#endif

  EndPoint* PIOXferFactory::
  createEndPoint(std::string& endpoint, bool local) {
    return new GppEndPoint(endpoint, 0, local);
  }

#if 0
void PIOXferFactory::releaseEndPoint( EndPoint* )
{}
#endif

// This method is used to allocate a transfer compatible SMB
SmemServices* PIOXferFactory::getSmemServices(EndPoint* loc )
{
  if ( loc->smem ) {
    return loc->smem;
  }
  return CreateSmemServices( this, loc);
}


/***************************************
 *  This method is used to create a transfer service object
 ***************************************/
XferServices* PIOXferFactory::getXferServices(SmemServices* source, SmemServices* target)
{
  return new PIOXferServices(source, target);
}


/***************************************
 *  This method is used to dynamically allocate
 *  an endpoint for an application running on "this"
 *  node.
 ***************************************/
static OCPI::OS::int32_t pid;
static OCPI::OS::int32_t smb_count=0;
std::string PIOXferFactory::
allocateEndpoint(const OCPI::Util::PValue*, unsigned mailBox, unsigned maxMailBoxes)
{
  OCPI::Util::SelfAutoMutex guard (this); 
  std::string ep;

  pid++;

  unsigned int size = m_SMBSize;

  pid = getpid();
  char tep[128];
  snprintf(tep,128,"ocpi-smb-pio://pioXfer%d%d:%d.%d.%d",pid,smb_count++,size, mailBox, maxMailBoxes);
  ep = tep;

  return ep;
}




// Sets smem location data based upon the specified endpoint
OCPI::OS::int32_t GppEndPoint::parse( std::string& ep )
{

  OCPI::OS::uint32_t n,i=0;
  OCPI::OS::int32_t start=0;
  char sname[80];
  for ( n=0; n<ep.length(); n++ ) {
    if ( (start<2) && (ep[n] == '/') ) {
      start++;
    }
    else if ( (start == 2) && (ep[n] == ':') ) {
      break;
    }
    else if ( start == 2 ) {
      sname[i++] = ep[n];
    }
  }

  sname[i] = 0;
  m_smb_name = sname;

  return 0;
}

GppEndPoint::~GppEndPoint()
{
}

void PIOXferRequest::modify( OCPI::OS::uint32_t new_offsets[], OCPI::OS::uint32_t old_offsets[] )
{
  int n=0;
  while ( new_offsets[n] ) {
    xfer_modify( m_thandle, &new_offsets[n], &old_offsets[n] );
    n++;
  }
}


// PIOXferRequest destructor implementation
PIOXferRequest::~PIOXferRequest ()
{
  if (m_thandle)
    {
      (void)xfer_release (m_thandle, 0);
    }
}


// Create tranfer services template
void PIOXferServices::createTemplate (SmemServices* p1, SmemServices* p2)
{

  m_txRequest = NULL;
  m_sourceSmb = p1;
  m_targetSmb = p2;

  // Invoke original code, saving the returned template reference.
  xfer_create (p1, p2, 0, &m_xftemplate);
}



XferRequest* PIOXferServices::createXferRequest()
{
  OCPI::Util::SelfAutoMutex guard (&parent()); 
  return new PIOXferRequest ( *this );
}


// Create a transfer request
XferRequest* PIOXferRequest::copy (OCPI::OS::uint32_t srcoffs, 
                                    OCPI::OS::uint32_t dstoffs, 
                                    OCPI::OS::uint32_t nbytes, 
                                    XferRequest::Flags flags
                                    )
{

  OCPI::OS::int32_t retVal = 0;
  OCPI::OS::int32_t newflags = 0;
  if (flags & XferRequest::FirstTransfer) newflags |= XFER_FIRST;
  if (flags & XferRequest::LastTransfer) newflags |= XFER_LAST;
  if ( getHandle() == NULL ) {
    retVal = xfer_copy ( parent().m_xftemplate, srcoffs, dstoffs, nbytes, newflags, &getHandle());
    if (retVal){
      return NULL;
    }
  }
  else {
    XF_transfer handle;
    retVal = xfer_copy ( parent().m_xftemplate, srcoffs, dstoffs, nbytes, newflags, &handle);
    if (retVal){
      return NULL;
    }
    XF_transfer handles[3];
    handles[0] = handle;
    handles[1] = getHandle();
    handles[2] = 0;
    retVal = xfer_group ( handles, 0, &getHandle());
    if (retVal) {
      return NULL;
    }
  }
  return this;
}


// Group data transfer requests
XferRequest & PIOXferRequest::group (XferRequest* lhs )
{
  XF_transfer handles[3];
  handles[0] = static_cast<PIOXferRequest*>(lhs)->getHandle();
  handles[1] = getHandle();
  handles[2] = 0;
  xfer_group ( handles, 0, &getHandle());
  return *this;
}




// Destructor
PIOXferServices::~PIOXferServices ()
{
  // Invoke destroy without flags.
  xfer_destroy (m_xftemplate, 0);
}

    RegisterTransferDriver<PIOXferFactory> pioDriver;

}
