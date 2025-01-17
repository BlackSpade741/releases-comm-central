/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var EXPORTED_SYMBOLS = ["MailContentHandler"];

// defined in nsIContentHandler.idl.
var NS_ERROR_WONT_HANDLE_CONTENT = 0x805d0001;

function MailContentHandler() {
  if (!gMailContentHandler) {
    gMailContentHandler = this;
  }
  return gMailContentHandler;
}

MailContentHandler.prototype = {
  QueryInterface: ChromeUtils.generateQI([Ci.nsIContentHandler]),

  openInExternal(uri) {
    Cc["@mozilla.org/uriloader/external-protocol-service;1"]
      .getService(Ci.nsIExternalProtocolService)
      .loadURI(uri);
  },

  // nsIContentHandler

  handleContent(aContentType, aWindowContext, aRequest) {
    try {
      if (
        !Cc["@mozilla.org/webnavigation-info;1"]
          .getService(Ci.nsIWebNavigationInfo)
          .isTypeSupported(aContentType, null)
      ) {
        throw NS_ERROR_WONT_HANDLE_CONTENT;
      }
    } catch (e) {
      throw NS_ERROR_WONT_HANDLE_CONTENT;
    }

    aRequest.QueryInterface(Ci.nsIChannel);

    // For internal protocols (e.g. imap, mailbox, mailto), we want to handle
    // them internally as we know what to do. For http and https we don't
    // actually deal with external windows very well, so we redirect them to
    // the external browser.
    if (!aRequest.URI.schemeIs("http") && !aRequest.URI.schemeIs("https")) {
      throw NS_ERROR_WONT_HANDLE_CONTENT;
    }

    this.openInExternal(aRequest.URI);
    aRequest.cancel(Cr.NS_BINDING_ABORTED);
  },

  // nsIFactory
  createInstance(outer, iid) {
    if (outer != null) {
      throw Cr.NS_ERROR_NO_AGGREGATION;
    }

    return this.QueryInterface(iid);
  },

  lockFactory(lock) {
    // No-op.
  },
};
var gMailContentHandler = new MailContentHandler();
