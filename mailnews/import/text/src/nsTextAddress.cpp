/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAbBaseCID.h"
#include "nsTextAddress.h"
#include "nsIAbDirectory.h"
#include "nsNativeCharsetUtils.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsNetUtil.h"
#include "nsMsgI18N.h"
#include "nsMsgUtils.h"
#include "nsIConverterInputStream.h"
#include "nsIUnicharLineInputStream.h"
#include "nsMsgUtils.h"

#include "ImportDebug.h"
#include "plstr.h"
#include "msgCore.h"

#define kWhitespace " \t\b\r\n"

nsTextAddress::nsTextAddress() {
  m_LFCount = 0;
  m_CRCount = 0;
}

nsTextAddress::~nsTextAddress() {}

nsresult nsTextAddress::GetUnicharLineStreamForFile(
    nsIFile *aFile, nsIInputStream *aInputStream,
    nsIUnicharLineInputStream **aStream) {
  nsAutoCString charset;
  nsresult rv = MsgDetectCharsetFromFile(aFile, charset);
  if (NS_FAILED(rv)) {
    charset = nsMsgI18NFileSystemCharset();
  }

  nsCOMPtr<nsIConverterInputStream> converterStream =
      do_CreateInstance("@mozilla.org/intl/converter-input-stream;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = converterStream->Init(
        aInputStream, charset.get(), 8192,
        nsIConverterInputStream::DEFAULT_REPLACEMENT_CHARACTER);
  }

  return CallQueryInterface(converterStream, aStream);
}

nsresult nsTextAddress::ImportAddresses(bool *pAbort, const char16_t *pName,
                                        nsIFile *pSrc,
                                        nsIAbDirectory *pDirectory,
                                        nsIImportFieldMap *fieldMap,
                                        nsString &errors, uint32_t *pProgress) {
  // Open the source file for reading, read each line and process it!
  m_directory = pDirectory;
  m_fieldMap = fieldMap;

  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputStream), pSrc);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening address file for reading\n");
    return rv;
  }

  // Here we use this to work out the size of the file, so we can update
  // an integer as we go through the file which will update a progress
  // bar if required by the caller.
  uint64_t bytesLeft = 0;
  rv = inputStream->Available(&bytesLeft);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error checking address file for size\n");
    inputStream->Close();
    return rv;
  }

  uint64_t totalBytes = bytesLeft;
  bool skipRecord = false;

  rv = m_fieldMap->GetSkipFirstRecord(&skipRecord);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0(
        "*** Error checking to see if we should skip the first record\n");
    return rv;
  }

  nsCOMPtr<nsIUnicharLineInputStream> lineStream;
  rv = GetUnicharLineStreamForFile(pSrc, inputStream,
                                   getter_AddRefs(lineStream));
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening converter stream for importer\n");
    return rv;
  }

  bool more = true;
  nsAutoString line;

  // Skip the first record if the user has requested it.
  if (skipRecord) rv = ReadRecord(lineStream, line, &more);

  while (!(*pAbort) && more && NS_SUCCEEDED(rv)) {
    // Read the line in
    rv = ReadRecord(lineStream, line, &more);
    if (NS_SUCCEEDED(rv)) {
      // Now process it to add it to the database
      rv = ProcessLine(line, errors);

      if (NS_FAILED(rv)) {
        IMPORT_LOG0("*** Error processing text record.\n");
      }
    }
    if (NS_SUCCEEDED(rv) && pProgress) {
      // This won't be totally accurate, but its the best we can do
      // considering that lineStream won't give us how many bytes
      // are actually left.
      bytesLeft -= line.Length();
      *pProgress = std::min(totalBytes - bytesLeft, uint64_t(PR_UINT32_MAX));
    }
  }

  inputStream->Close();

  if (NS_FAILED(rv)) {
    IMPORT_LOG0(
        "*** Error reading the address book - probably incorrect ending\n");
    return NS_ERROR_FAILURE;
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsTextAddress::ReadRecord(nsIUnicharLineInputStream *aLineStream,
                                   nsAString &aLine, bool *aMore) {
  bool more = true;
  uint32_t numQuotes = 0;
  nsresult rv;
  nsAutoString line;

  // ensure aLine is empty
  aLine.Truncate();

  do {
    if (!more) {
      // No more, so we must have an incorrect file.
      rv = NS_ERROR_FAILURE;
    } else {
      // Read the line and append it
      rv = aLineStream->ReadLine(line, &more);
      if (NS_SUCCEEDED(rv)) {
        if (!aLine.IsEmpty()) aLine.AppendLiteral(MSG_LINEBREAK);
        aLine.Append(line);

        numQuotes += MsgCountChar(line, char16_t('"'));
      }
    }
    // Continue whilst everything is ok, and we have an odd number of quotes.
  } while (NS_SUCCEEDED(rv) && (numQuotes % 2 != 0));

  *aMore = more;
  return rv;
}

nsresult nsTextAddress::ReadRecordNumber(nsIFile *aSrc, nsAString &aLine,
                                         int32_t rNum) {
  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputStream), aSrc);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening address file for reading\n");
    return rv;
  }

  int32_t rIndex = 0;
  uint64_t bytesLeft = 0;

  rv = inputStream->Available(&bytesLeft);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error checking address file for eof\n");
    inputStream->Close();
    return rv;
  }

  nsCOMPtr<nsIUnicharLineInputStream> lineStream;
  rv = GetUnicharLineStreamForFile(aSrc, inputStream,
                                   getter_AddRefs(lineStream));
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening converter stream for importer\n");
    return rv;
  }

  bool more = true;

  while (more && (rIndex <= rNum)) {
    rv = ReadRecord(lineStream, aLine, &more);
    if (NS_FAILED(rv)) {
      inputStream->Close();
      return rv;
    }
    if (rIndex == rNum) {
      inputStream->Close();
      return NS_OK;
    }

    rIndex++;
  }

  return NS_ERROR_FAILURE;
}

int32_t nsTextAddress::CountFields(const nsAString &aLine, char16_t delim) {
  int32_t pos = 0;
  int32_t maxLen = aLine.Length();
  int32_t count = 0;
  char16_t tab = char16_t('\t');
  char16_t doubleQuote = char16_t('"');

  if (delim == tab) tab = char16_t('\0');

  while (pos < maxLen) {
    while (((aLine[pos] == char16_t(' ')) || (aLine[pos] == tab)) &&
           (pos < maxLen)) {
      pos++;
    }
    if ((pos < maxLen) && (aLine[pos] == doubleQuote)) {
      pos++;
      while ((pos < maxLen) && (aLine[pos] != doubleQuote)) {
        pos++;
        if (((pos + 1) < maxLen) && (aLine[pos] == doubleQuote) &&
            (aLine[pos + 1] == doubleQuote)) {
          pos += 2;
        }
      }
      if (pos < maxLen) pos++;
    }
    while ((pos < maxLen) && (aLine[pos] != delim)) pos++;

    count++;
    pos++;
  }

  return count;
}

bool nsTextAddress::GetField(const nsAString &aLine, int32_t index,
                             nsString &field, char16_t delim) {
  bool result = false;
  int32_t pos = 0;
  int32_t maxLen = aLine.Length();
  char16_t tab = char16_t('\t');
  char16_t doubleQuote = char16_t('"');

  field.Truncate();

  if (delim == tab) tab = 0;

  while (index && (pos < maxLen)) {
    while (((aLine[pos] == char16_t(' ')) || (aLine[pos] == tab)) &&
           (pos < maxLen)) {
      pos++;
    }
    if (pos >= maxLen) break;
    if (aLine[pos] == doubleQuote) {
      do {
        pos++;
        if (((pos + 1) < maxLen) && (aLine[pos] == doubleQuote) &&
            (aLine[pos + 1] == doubleQuote)) {
          pos += 2;
        }
      } while ((pos < maxLen) && (aLine[pos] != doubleQuote));
      if (pos < maxLen) pos++;
    }
    if (pos >= maxLen) break;

    while ((pos < maxLen) && (aLine[pos] != delim)) pos++;

    if (pos >= maxLen) break;

    index--;
    pos++;
  }

  if (pos >= maxLen) return result;

  result = true;

  while ((pos < maxLen) && ((aLine[pos] == ' ') || (aLine[pos] == tab))) pos++;

  int32_t fLen = 0;
  int32_t startPos = pos;
  bool quoted = false;
  if (aLine[pos] == '"') {
    startPos++;
    fLen = -1;
    do {
      pos++;
      fLen++;
      if (((pos + 1) < maxLen) && (aLine[pos] == doubleQuote) &&
          (aLine[pos + 1] == doubleQuote)) {
        quoted = true;
        pos += 2;
        fLen += 2;
      }
    } while ((pos < maxLen) && (aLine[pos] != doubleQuote));
  } else {
    while ((pos < maxLen) && (aLine[pos] != delim)) {
      pos++;
      fLen++;
    }
  }

  if (!fLen) {
    return result;
  }

  field.Append(nsDependentSubstring(aLine, startPos, fLen));
  field.Trim(kWhitespace);

  if (quoted) {
    int32_t offset = field.Find("\"\"");
    while (offset != -1) {
      field.Cut(offset, 1);
      offset = MsgFind(field, "\"\"", false, offset + 1);
    }
  }

  return result;
}

nsresult nsTextAddress::DetermineDelim(nsIFile *aSrc) {
  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputStream), aSrc);
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening address file for reading\n");
    return rv;
  }

  int32_t lineCount = 0;
  int32_t tabCount = 0;
  int32_t commaCount = 0;
  int32_t tabLines = 0;
  int32_t commaLines = 0;
  nsAutoString line;
  bool more = true;

  nsCOMPtr<nsIUnicharLineInputStream> lineStream;
  rv = GetUnicharLineStreamForFile(aSrc, inputStream,
                                   getter_AddRefs(lineStream));
  if (NS_FAILED(rv)) {
    IMPORT_LOG0("*** Error opening converter stream for importer\n");
    return rv;
  }

  while (more && NS_SUCCEEDED(rv) && (lineCount < 100)) {
    rv = lineStream->ReadLine(line, &more);
    if (NS_SUCCEEDED(rv)) {
      tabCount = CountFields(line, char16_t('\t'));
      commaCount = CountFields(line, char16_t(','));
      if (tabCount > commaCount)
        tabLines++;
      else if (commaCount)
        commaLines++;
    }
    lineCount++;
  }

  rv = inputStream->Close();

  if (tabLines > commaLines)
    m_delim = char16_t('\t');
  else
    m_delim = char16_t(',');

  IMPORT_LOG2("Tab count = %d, Comma count = %d\n", tabLines, commaLines);

  return rv;
}

/*
    This is where the real work happens!
    Go through the field map and set the data in a new database row
*/
nsresult nsTextAddress::ProcessLine(const nsAString &aLine, nsString &errors) {
  if (!m_fieldMap) {
    IMPORT_LOG0("*** Error, text import needs a field map\n");
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

  // Wait until we get our first non-empty field, then create a new row,
  // fill in the data, then add the row to the database.
  nsCOMPtr<nsIAbCard> newCard;
  nsAutoString fieldVal;
  int32_t fieldNum;
  int32_t numFields = 0;
  bool active;
  rv = m_fieldMap->GetMapSize(&numFields);
  for (int32_t i = 0; (i < numFields) && NS_SUCCEEDED(rv); i++) {
    active = false;
    rv = m_fieldMap->GetFieldMap(i, &fieldNum);
    if (NS_SUCCEEDED(rv)) rv = m_fieldMap->GetFieldActive(i, &active);
    if (NS_SUCCEEDED(rv) && active) {
      if (GetField(aLine, i, fieldVal, m_delim)) {
        if (!fieldVal.IsEmpty()) {
          if (!newCard) {
            newCard = do_CreateInstance(NS_ABCARDPROPERTY_CONTRACTID, &rv);
          }
          if (newCard) {
            rv = m_fieldMap->SetFieldValue(m_directory, newCard, fieldNum,
                                           fieldVal);
          }
        }
      } else {
        break;
      }
    } else if (active) {
      IMPORT_LOG1("*** Error getting field map for index %ld\n", (long)i);
    }
  }

  nsIAbCard *outCard;
  rv = m_directory->AddCard(newCard, &outCard);

  return rv;
}
