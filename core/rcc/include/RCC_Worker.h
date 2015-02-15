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



/**
  @file

  @brief
    Contains the declarations of the RCC Worker interface as specified in
    the CP289U specification.

************************************************************************** */


#ifndef OCPI_RCC_WORKER__INTERFACE_H
#define OCPI_RCC_WORKER__INTERFACE_H

#include <stdio.h>
#include <stddef.h>
#ifdef __cplusplus
// For proxy slaves
#include "OcpiContainerApi.h"
#endif
#if defined (WIN32)
    /**
     * 8 bit signed integer data type.
     */

    typedef signed char int8_t;

    /**
     * 8 bit unsigned integer data type.
     */

    typedef unsigned char uint8_t;

    /**
     * 16 bit signed integer data type.
     */

    typedef short int16_t;

    /**
     * 16 bit unsigned integer data type.
     */

    typedef unsigned short uint16_t;

    /**
     * 32 bit signed integer data type.
     */

    typedef int int32_t;

    /**
     * 32 bit unsigned integer data type.
     */

    typedef unsigned int uint32_t;

    /**
     * 64 bit signed integer data type.
     */

    typedef long long int64_t;

    /**
     * 64 bit unsigned integer data type.
     */

    typedef unsigned long long uint64_t;
#else
#ifndef _WRS_KERNEL
#include <stdint.h>
#endif
#endif

#ifdef WORKER_INTERNAL
namespace OCPI { namespace RCC { class Port; } namespace DataTransport { class BufferUserFacet;}}
#define RCC_CONST
#else
#define RCC_CONST const
#endif
#ifdef __cplusplus
namespace OCPI {
  namespace API {
    class Application;
  }
  namespace RCC {
#endif

typedef uint16_t  RCCOrdinal;
typedef uint8_t   RCCOpCode;
typedef enum {
  RCC_LITTLE,
  RCC_BIG,
  RCC_DYNAMIC
} RCCEndian;
#ifdef __cplusplus
typedef bool RCCBoolean;

#else
typedef uint8_t   RCCBoolean;
#endif
typedef void      *RCCBufferId;
typedef float     RCCFloat;
typedef double    RCCDouble;
typedef char      RCCChar;
typedef uint64_t  RCCTime;

// do compile time checks for float, double, and char
#define RCC_VERSION 1
#define RCC_NO_EXCEPTION (0)
#define RCC_SYSTEM_EXCEPTION (1)
#define RCC_NO_ORDINAL ((RCCOrdinal)(-1))

typedef enum {
  RCC_OK,
  RCC_ERROR,
  RCC_FATAL,
  RCC_DONE,
  RCC_ADVANCE,
  RCC_ADVANCE_DONE
} RCCResult;

typedef uint32_t RCCPortMask;
typedef struct RCCWorker RCCWorker;
typedef struct RCCPort RCCPort;

typedef struct {
  RCCPortMask *portMasks;
  RCCBoolean   timeout;
  uint32_t     usecs;
} RCCRunCondition;

#ifdef __cplusplus
struct RunCondition {
  RCCPortMask *m_portMasks;  // the masks used for checking
  RCCPortMask  m_myMasks[3]; // non-allocated masks used almost all the time
  RCCBoolean   m_timeout;    // is timeout enabled?
  uint32_t     m_usecs;      // usecs of timeout, zero is valid
  RCCPortMask *m_allocated;  // NULL or allocated
  RCCPortMask  m_allMasks;   // summary of all masks in the list
  // Constructors
  // Default constructor: no timeout, all ports must be ready
  RunCondition();
  // This allows a zero-terminated list of masks to be provided in the argument list.
  // No timeout is enabled.  A very common case.  If given one arg == 0, then never runs
  RunCondition(RCCPortMask first, ...);
  // This allows the specification of a mask array (which can be nullptr) and a timeout.
  RunCondition(RCCPortMask*, uint32_t usecs = 0, bool timeout = false);
  ~RunCondition();
  // initialize the default run condition, given how many ports there are
  // assume default contructor has already been run
  inline void initDefault(unsigned nPorts) {
    m_myMasks[0] = ~(-1 << nPorts);
    m_myMasks[1] = 0;
    m_portMasks = nPorts ? m_myMasks : NULL;
    m_allMasks = m_myMasks[0];
  }
  // Compatibility hack to support older C-langage run conditions
  inline void setRunCondition(const RCCRunCondition &crc) {
    m_portMasks = crc.portMasks;
    m_timeout = crc.timeout;
    m_usecs = crc.usecs;
    m_allMasks = 0;
    for (RCCPortMask *pm = m_portMasks; *pm; pm++)
      m_allMasks |= *pm;
  }
  // Disable the timeout, without changing its value
  inline void disableTimeout() { m_timeout = false; }
  // Enable the timeout, setting its value
  inline void enableTimeout(uint32_t usecs) { m_timeout = true; m_usecs = usecs; }
  // Enable the tinmeout, without changing its value
  inline void enableTimeout() { m_timeout = true; }
  inline void setTimeout(uint32_t usecs) { m_usecs = usecs; }
};
#endif
typedef RCCResult RCCMethod(RCCWorker *_this);
typedef RCCResult RCCRunMethod(RCCWorker *_this,
			       RCCBoolean timedout,
			       RCCBoolean *newRunCondition);
typedef RCCResult RCCPortMethod(RCCWorker *_this,
				RCCPort *port,
				RCCResult reason);
 typedef struct {
   size_t length;
   size_t offset;
   size_t left;
   size_t right;
 } RCCPartInfo;

typedef struct {
  void *data;
  size_t maxLength;
  RCCPartInfo *partInfo;

  /* private member for container use */
  size_t length_;
  RCCOpCode opCode_;
#ifdef WORKER_INTERNAL
  OCPI::DataTransport::BufferUserFacet *containerBuffer;
#else
  void *id_; 
#endif
} RCCBuffer;

struct RCCPort {
  RCCBuffer current;

  RCC_CONST struct {
    size_t length;
    union {
      RCCOpCode operation;
      RCCOpCode exception;
    } u;
  } input;
  struct {
    size_t length;
    union {
      RCCOpCode operation;
      RCCOpCode exception;
    } u;
  } output;
  RCCPortMethod *callBack;

  /* Used by the container */
  RCCBoolean useDefaultLength_; // for C++, use the length field as default
  size_t defaultLength_;
  RCCBoolean useDefaultOpCode_; // for C++, use the length field as default
  RCCOpCode defaultOpCode_;
#ifdef WORKER_INTERNAL
  OCPI::RCC::Port *containerPort;
#else
  void* opaque;
#endif
};

typedef struct {
  void (*release)(RCCBuffer *);
  void (*send)(RCCPort *, RCCBuffer*, RCCOpCode op, size_t length);
  RCCBoolean (*request)(RCCPort *port, size_t maxlength);
  RCCBoolean (*advance)(RCCPort *port, size_t maxlength);
  RCCBoolean (*wait)(RCCPort *, size_t max, unsigned usecs);
  void (*take)(RCCPort *,RCCBuffer *old_buffer, RCCBuffer *new_buffer);
  RCCResult (*setError)(const char *, ...);
} RCCContainer;

struct RCCWorker {
  void * RCC_CONST         properties;
  void * RCC_CONST       * RCC_CONST memories;
  void * RCC_CONST         memory;
  RCC_CONST RCCContainer   container;
  RCCRunCondition        * runCondition;
  RCCPortMask              connectedPorts;
  char                   * errorString;
  RCCPort                  ports[1];
};


typedef struct {
  RCCOrdinal port;
  size_t maxLength;
  unsigned minBuffers;
  // others...
} RCCPortInfo;

typedef struct {
  /* Information for consistency checking */
  unsigned version, numInputs, numOutputs;
  size_t propertySize, *memSizes;
  RCCBoolean threadProfile;
  /* Methods */
  RCCMethod *initialize, *start, *stop, *release, *test,
    *afterConfigure, *beforeQuery;
  RCCRunMethod *run;
  /* Implementation information for container behavior */
  RCCRunCondition *runCondition;
  RCCPortInfo *portInfo;

  /* Mask indicating which ports may be left unconnected */
  RCCPortMask optionallyConnectedPorts;
  size_t memSize;
} RCCDispatch;

// Info common to C and C++
typedef struct {
  size_t size, memSize, *memSizes, propertySize;
  RCCPortInfo *portInfo;
  RCCPortMask optionallyConnectedPorts; // usually initialized from metadata
} RCCWorkerInfo;

typedef struct {
  const char *name;
  RCCDispatch *dispatch;
  const char *type;
} RCCEntryTable;

#ifdef __cplusplus
// This is a preliminary implementation that avoids reorganizing the classes for
// maximum commonality between C and C++ workers. FIXME
 class RCCUserWorker;
 typedef RCCUserWorker *RCCConstruct(void *place, RCCWorkerInfo &info);
 class RCCUserPort;

 class RCCUserBufferInterface {
 protected:
   virtual void setRccBuffer(RCCBuffer *b) = 0;
   virtual RCCBuffer *getRccBuffer() const = 0;
 public:
   virtual void * data() const = 0;
   virtual size_t maxLength() const = 0;
   // For input buffers
   virtual size_t length() const = 0;
   virtual RCCOpCode opCode() const = 0;
   // For output buffers
   virtual void setLength(size_t length) = 0;
   virtual void setOpCode(RCCOpCode op) = 0;
   virtual void setInfo(RCCOpCode op, size_t len) = 0;
   virtual void release() = 0;

 };

 class RCCUserBuffer : public RCCUserBufferInterface {
   RCCBuffer *m_rccBuffer;
   RCCBuffer  m_taken;
   friend class RCCUserPort;
   friend class RCCPortOperation;
 protected:
   RCCUserBuffer();
   virtual ~RCCUserBuffer();
   void setRccBuffer(RCCBuffer *b);
   inline RCCBuffer *getRccBuffer() const { return m_rccBuffer; }
 public:
   inline void * data() const { return m_rccBuffer->data; }
   inline size_t maxLength() const { return m_rccBuffer->maxLength; }
   // For input buffers
   inline size_t length() const { return m_rccBuffer->length_; }
   RCCOpCode opCode() const { return m_rccBuffer->opCode_; }
   // For output buffers
   void setLength(size_t length) { m_rccBuffer->length_ = length; }
   void setOpCode(RCCOpCode op) {m_rccBuffer->opCode_ = op; }
   void setInfo(RCCOpCode op, size_t len) {
     setOpCode(op);
     setLength(len);
   }
   void release();
 };


 class RCCPortOperation : public RCCUserBufferInterface {
 protected:
   RCCPortOperation() : m_buffer(NULL) {}
   RCCUserBuffer *m_buffer;
   inline void setRccBuffer(RCCBuffer *b) { m_buffer->setRccBuffer(b); };
   inline RCCBuffer *getRccBuffer() const { return m_buffer->getRccBuffer(); }
 public:
   inline void * data() const { return m_buffer->data(); }
   inline size_t maxLength() const { return m_buffer->maxLength(); }
   // For input buffers
   inline size_t length() const { return m_buffer->length(); }
   inline RCCOpCode opCode() const  { return m_buffer->opCode(); };
   // For output buffers
   inline void setLength(size_t length) { m_buffer->setLength(length); }
   inline void setOpCode(RCCOpCode op) { m_buffer->setOpCode(op); }
   inline void setInfo(RCCOpCode op, size_t len) { m_buffer->setInfo(op, len); }
   inline void release() { m_buffer->release(); }

   unsigned  overlap() const;
   bool      endOfWhole() const;
   bool      endOfStream() const;
   unsigned  wholeSize() const;  // Size of the whole for this message

 };

 // Port inherits the buffer class in order to act as current buffer
 class RCCUserPort : public RCCUserBuffer {
   RCCPort &m_rccPort;
   friend class RCCUserWorker;
 protected:
   RCCUserPort();
 public:
   inline bool hasBuffer() const { return m_rccBuffer != NULL; }
   size_t
     topLength(size_t elementLength);
   void
     checkLength(size_t length),
     setDefaultLength(size_t length),
     setDefaultOpCode(RCCOpCode op),
     send(RCCUserBuffer&);
   RCCUserBuffer &take(RCCUserBuffer *oldBuffer = NULL);
   bool
    request(size_t maxlength = 0),
    advance(size_t maxlength = 0),
    isConnected(),
    wait(size_t max, unsigned usecs);
   RCCOrdinal ordinal() const;
 };


 class Worker;
 class RCCUserWorker {
   friend class Worker;
   Worker &m_worker;
   RCCUserPort *m_ports; // array of C++ port objects
 protected:
   bool m_first;
   RCCWorker &m_rcc;
   RCCUserWorker();
   virtual ~RCCUserWorker();

   // These are called by the worker.

   RCCUserPort &getPort(unsigned n) const { return m_ports[n]; }
   inline bool firstRun() const { return m_first; };
   // access the current run condition
   const RunCondition *getRunCondition() const;
   // Change the current run condition - if NULL, revert to the default run condition
   void setRunCondition(const RunCondition *rc);
   virtual uint8_t *rawProperties(size_t &size) const;
   RCCResult setError(const char *fmt, ...);
   OCPI::API::Application &getApplication();
   // These below are called by the container, and NOT by the worker.

   // The worker author implements any of these that have non-default behavior.
   // There is a default implementation for all of them.
   virtual RCCResult
     initialize(), start(), stop(), release(), test(), beforeQuery(), afterConfigure();
   // The worker author must implement this one.
   virtual RCCResult run(bool timedOut) = 0;
   virtual RCCTime getTime();
   
 };
 // This class emulates the API worker, and is customized for the specific implementation
 // It forwards all the API methods
 class RCCUserSlave {
 protected:
   OCPI::API::Worker &m_worker;
   RCCUserSlave();
   //   virtual ~RCCUserSlave();
 public:
   inline void start() { m_worker.start(); }
   inline void stop() { m_worker.stop(); }
   inline void beforeQuery() { m_worker.beforeQuery(); }
   inline void afterConfigure() { m_worker.afterConfigure(); }
   // Untyped property setting - slowest but convenient
   inline void setProperty(const char *name, const char *value) {
     m_worker.setProperty(name, value);
   }
   // Untyped property list setting - slow but convenient
   inline void setProperties(const char *props[][2]) {
     m_worker.setProperties(props);
   }
   // Typed property list setting - slightly safer, still slow
   inline void setProperties(const OCPI::API::PValue *props) { m_worker.setProperties(props); }
   inline bool getProperty(unsigned ordinal, std::string &name, std::string &value,
			   bool *unreadablep = NULL, bool hex = false) {
     return m_worker.getProperty(ordinal, name, value, unreadablep, hex);
   }
 };
}} // end of namespace OCPI::RCC
#endif
#endif