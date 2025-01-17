/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsLDAPConnection_h_
#define _nsLDAPConnection_h_

#include "nsILDAPConnection.h"
#include "ldap.h"
#include "nsString.h"
#include "nsIThread.h"
#include "nsIRunnable.h"
#include "nsCOMPtr.h"
#include "nsILDAPMessageListener.h"
#include "nsInterfaceHashtable.h"
#include "nspr.h"
#include "nsWeakReference.h"
#include "nsIDNSListener.h"
#include "nsICancelable.h"
#include "nsIRequest.h"
#include "nsCOMArray.h"
#include "nsIObserver.h"
#include "nsAutoPtr.h"
#include "mozilla/Mutex.h"

/**
 * Casting nsILDAPConnection to nsISupports is ambiguous.
 * This method handles that.
 */
inline nsISupports *ToSupports(nsILDAPConnection *p) {
  return NS_ISUPPORTS_CAST(nsILDAPConnection *, p);
}

// 0d871e30-1dd2-11b2-8ea9-831778c78e93
//
#define NS_LDAPCONNECTION_CID                        \
  {                                                  \
    0x0d871e30, 0x1dd2, 0x11b2, {                    \
      0x8e, 0xa9, 0x83, 0x17, 0x78, 0xc7, 0x8e, 0x93 \
    }                                                \
  }

class nsLDAPConnection : public nsILDAPConnection,
                         public nsSupportsWeakReference,
                         public nsIDNSListener,
                         public nsIObserver

{
  friend class nsLDAPOperation;
  friend class nsLDAPMessage;
  friend class nsLDAPConnectionRunnable;
  typedef mozilla::Mutex Mutex;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSILDAPCONNECTION
  NS_DECL_NSIDNSLISTENER
  NS_DECL_NSIOBSERVER

  // constructor & destructor
  //
  nsLDAPConnection();

  /**
   * Add an nsILDAPOperation to the list of operations pending on
   * this connection.  This is mainly intended for use by the
   * nsLDAPOperation code.  Used so that the thread waiting on messages
   * for this connection has an operation to callback to.
   *
   * @param aOperation                    operation to add
   * @exception NS_ERROR_ILLEGAL_VALUE    aOperation was NULL
   * @exception NS_ERROR_UNEXPECTED       this operation's msgId was not
   *                                      unique to this connection
   * @exception NS_ERROR_OUT_OF_MEMORY    out of memory
   */
  nsresult AddPendingOperation(uint32_t aOperationID,
                               nsILDAPOperation *aOperation);

  /**
   * Remove an nsILDAPOperation from the list of operations pending on this
   * connection.  Mainly intended for use by the nsLDAPOperation code.
   *
   * @param aOperation        operation to add
   * @exception NS_ERROR_INVALID_POINTER  aOperation was NULL
   * @exception NS_ERROR_OUT_OF_MEMORY    out of memory
   * @exception NS_ERROR_FAILURE          could not delete the operation
   */
  nsresult RemovePendingOperation(uint32_t aOperationID);

 protected:
  virtual ~nsLDAPConnection();

  /** invoke the callback associated with a given message, and possibly
   * delete it from the connection queue.
   */
  nsresult InvokeMessageCallback(LDAPMessage *aMsgHandle, nsILDAPMessage *aMsg,
                                 int32_t aOperation, bool aRemoveOpFromConnQ);

  /**
   * Dispatch an operation to the socket thread. This is intended for use by
   * the nsLDAPOperation code.
   */
  nsresult StartOp(nsIRunnable *aOp);

  void Close();             // close the connection
  LDAP *mConnectionHandle;  // the LDAP C SDK's connection object
  nsCString mBindName;      // who to bind as

  // We'll be dispatching operations on the SocketTransportService. This is
  // because there might be some SSL/TLS security handshaking happening
  // under the hood (the handshaking is deferred until the first IO Send).
  // The handshaking expects to be running on the STS thread (see Bug 1576364).
  // It also saves us spinning up a new thread to handle LDAP IO.
  nsCOMPtr<nsIEventTarget> mSTS;

  Mutex mPendingOperationsMutex;
  nsInterfaceHashtable<nsUint32HashKey, nsILDAPOperation> mPendingOperations;

  int32_t mPort;      // The LDAP port we're binding to
  bool mSSL;          // the options
  uint32_t mVersion;  // LDAP protocol version

  nsCString mResolvedIP;  // Preresolved list of host IPs
  nsCOMPtr<nsILDAPMessageListener> mInitListener;  // Init callback
  nsCOMPtr<nsICancelable> mDNSRequest;             // The "active" DNS request
  nsCString mDNSHost;  // The hostname being resolved
  nsCOMPtr<nsISupports>
      mClosure;  // private parameter (anything caller desires)
};

class nsLDAPConnectionRunnable : public nsIRunnable {
  friend class nsLDAPConnection;
  friend class nsLDAPMessage;

 public:
  nsLDAPConnectionRunnable(int32_t aOperationID, nsILDAPOperation *aOperation,
                           nsLDAPConnection *aConnection);

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  int32_t mOperationID;
  RefPtr<nsLDAPConnection> mConnection;

 private:
  virtual ~nsLDAPConnectionRunnable();
};

#endif  // _nsLDAPConnection_h_
