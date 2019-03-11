/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {MailServices} = ChromeUtils.import("resource:///modules/MailServices.jsm");
const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");
const {Color} = ChromeUtils.import("resource://gre/modules/Color.jsm");

var EXPORTED_SYMBOLS = ["TagUtils"];

var TagUtils = {
  loadTagsIntoCSS,
  addTagToAllDocumentSheets,
  isColorContrastEnough,
  getColor,
};

function loadTagsIntoCSS(aDocument) {
  let tagSheet = findTagColorSheet(aDocument);
  let tagArray = MailServices.tags.getAllTags({});
  for (let tag of tagArray) {
    // tag.key is the internal key, like "$label1" for "Important".
    // For user defined keys with non-ASCII characters, key is
    // the MUTF-7 encoded name.
    addTagToSheet(tag.key, tag.color, tagSheet);
  }
}

function addTagToAllDocumentSheets(aKey, aColor) {
  let windowList = Services.wm.getEnumerator("mail:3pane", true);
  while (windowList.hasMoreElements()) {
    let nextWin = windowList.getNext();
    addTagToSheet(aKey, aColor, findTagColorSheet(nextWin.document));
  }

  windowList = Services.wm.getEnumerator("mailnews:search", true);
  while (windowList.hasMoreElements()) {
    let nextWin = windowList.getNext();
    addTagToSheet(aKey, aColor, findTagColorSheet(nextWin.document));
  }
}

function addTagToSheet(aKey, aColor, aSheet) {
  if (!aSheet)
    return;

  // Add rules to sheet.
  let selector = MailServices.tags.getSelectorForKey(aKey);
  let ruleString1 = "treechildren::-moz-tree-row(" + selector +
                    ", selected, focus) { background-color: " + aColor + " !important; }";
  let ruleString2 = "treechildren::-moz-tree-cell-text(" + selector +
                    ") { color: " + aColor + "; }";
  let textColor = "black";
  if (!isColorContrastEnough(aColor)) {
    textColor = "white";
  }
  let ruleString3 = "treechildren::-moz-tree-cell-text(" + selector +
                    ", selected, focus) { color: " + textColor + "; }";
  try {
    aSheet.insertRule(ruleString1, aSheet.cssRules.length);
    aSheet.insertRule(ruleString2, aSheet.cssRules.length);
    aSheet.insertRule(ruleString3, aSheet.cssRules.length);
  } catch (ex) {
    aSheet.ownerNode.addEventListener("load",
                                      () => addTagToSheet(aKey, aColor, aSheet),
                                      { once: true });
  }
}

function findTagColorSheet(aDocument) {
  const cssUri = "chrome://messenger/skin/tagColors.css";
  let tagSheet = null;
  for (let sheet of aDocument.styleSheets) {
    if (sheet.href == cssUri) {
      tagSheet = sheet;
      break;
    }
  }
  if (!tagSheet) {
    Cu.reportError("TagUtils.findTagColorSheet: tagColors.css not found");
  }
  return tagSheet;
}

// Here comes some stuff that was originally in Windows8WindowFrameColor.jsm.

/* Checks if black writing on 'aColor' background has enough contrast */
function isColorContrastEnough(aColor) {
  let bgColor = getColor(aColor);
  return new Color(...bgColor).isContrastRatioAcceptable(new Color(0, 0, 0));
}

function getColor(customizationColorHex, colorizationColorBalance) {
  // Zero-pad the number just to make sure that it is 8 digits.
  customizationColorHex = ("00000000" + customizationColorHex).substr(-8);
  let customizationColorArray = customizationColorHex.match(/../g);
  let [, fgR, fgG, fgB] = customizationColorArray.map(val => parseInt(val, 16));

  if (colorizationColorBalance == undefined) {
    colorizationColorBalance = 78;
  }

  // Window frame base color when Color Intensity is at 0, see bug 1004576.
  let frameBaseColor = 217;
  let alpha = colorizationColorBalance / 100;

  // Alpha-blend the foreground color with the frame base color.
  let r = Math.round(fgR * alpha + frameBaseColor * (1 - alpha));
  let g = Math.round(fgG * alpha + frameBaseColor * (1 - alpha));
  let b = Math.round(fgB * alpha + frameBaseColor * (1 - alpha));
  return [r, g, b];
}