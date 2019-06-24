/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNewMailnewsURI.h"
#include "nsURLHelper.h"
#include "nsSimpleURI.h"
#include "nsStandardURL.h"
#include "nsThreadUtils.h"
#include "MainThreadUtils.h"
#include "mozilla/SyncRunnable.h"
#include "nsIMsgProtocolHandler.h"
#include "nsIComponentRegistrar.h"

#include "../../local/src/nsPop3Service.h"
#include "../../local/src/nsMailboxService.h"
#include "../../compose/src/nsSmtpService.h"
#include "../../../ldap/xpcom/src/nsLDAPURL.h"
#include "../../imap/src/nsImapService.h"
#include "../../news/src/nsNntpService.h"
#include "../../addrbook/src/nsAddbookProtocolHandler.h"
#include "../src/nsCidProtocolHandler.h"

nsresult NS_NewMailnewsURI(nsIURI** aURI, const nsACString& aSpec,
                           const char* aCharset /* = nullptr */,
                           nsIURI* aBaseURI /* = nullptr */,
                           nsIIOService* aIOService /* = nullptr */) {
  nsAutoCString scheme;
  nsresult rv = net_ExtractURLScheme(aSpec, scheme);
  if (NS_FAILED(rv)) {
    // then aSpec is relative
    if (!aBaseURI) {
      return NS_ERROR_MALFORMED_URI;
    }

    rv = aBaseURI->GetScheme(scheme);
    if (NS_FAILED(rv)) return rv;
  }

  if (scheme.EqualsLiteral("mailbox") ||
      scheme.EqualsLiteral("mailbox-message")) {
    return nsMailboxService::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("imap") || scheme.EqualsLiteral("imap-message")) {
    return nsImapService::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("smtp") || scheme.EqualsLiteral("smtps")) {
    return nsSmtpService::NewSmtpURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("mailto")) {
    return nsSmtpService::NewMailtoURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("pop") || scheme.EqualsLiteral("pop3")) {
    return nsPop3Service::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("news") || scheme.EqualsLiteral("snews") ||
      scheme.EqualsLiteral("news-message") || scheme.EqualsLiteral("nntp")) {
    return nsNntpService::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("cid")) {
    return nsCidProtocolHandler::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("addbook")) {
    return nsAddbookProtocolHandler::NewURI(aSpec, aCharset, aBaseURI, aURI);
  }
  if (scheme.EqualsLiteral("ldap") || scheme.EqualsLiteral("ldaps")) {
    nsCOMPtr<nsILDAPURL> url = do_CreateInstance(NS_LDAPURL_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = url->Init(nsIStandardURL::URLTYPE_STANDARD,
                   scheme.EqualsLiteral("ldap") ? 389 : 636, aSpec, aCharset,
                   aBaseURI);
    NS_ENSURE_SUCCESS(rv, rv);
    url.forget(aURI);
    return NS_OK;
  }
  if (scheme.EqualsLiteral("smile")) {
    return NS_MutateURI(new mozilla::net::nsSimpleURI::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }
  if (scheme.EqualsLiteral("moz-cal-handle-itip")) {
    return NS_MutateURI(new mozilla::net::nsStandardURL::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }
  if (scheme.EqualsLiteral("webcal") || scheme.EqualsLiteral("webcals")) {
    return NS_MutateURI(new mozilla::net::nsStandardURL::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }

  rv = NS_ERROR_UNKNOWN_PROTOCOL;  // Let M-C handle it by default.

  nsCOMPtr<nsIComponentRegistrar> compMgr;
  NS_GetComponentRegistrar(getter_AddRefs(compMgr));
  if (compMgr) {
    nsAutoCString contractID(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX);
    contractID += scheme;
    bool isRegistered = false;
    compMgr->IsContractIDRegistered(contractID.get(), &isRegistered);
    if (isRegistered) {
      auto NewURI =
          [&aSpec, &aCharset, &aBaseURI, aURI, &contractID, &rv ]() -> auto {
        nsCOMPtr<nsIMsgProtocolHandler> handler(
            do_GetService(contractID.get()));
        if (handler) {
          // We recognise this URI. Use the protocol handler's result.
          rv = handler->NewURI(aSpec, aCharset, aBaseURI, aURI);
        }
      };
      if (NS_IsMainThread()) {
        NewURI();
      } else {
        nsCOMPtr<nsIRunnable> task = NS_NewRunnableFunction("NewURI", NewURI);
        mozilla::SyncRunnable::DispatchToThread(
            mozilla::GetMainThreadEventTarget(), task);
      }
    }
  }

  return rv;
}
