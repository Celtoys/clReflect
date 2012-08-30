//===--- TextDiagnostic.cpp - Text Diagnostic Pretty-Printing -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/ConvertUTF.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Locale.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include <algorithm>

using namespace clang;

static const enum raw_ostream::Colors noteColor =
  raw_ostream::BLACK;
static const enum raw_ostream::Colors fixitColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors caretColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors warningColor =
  raw_ostream::MAGENTA;
static const enum raw_ostream::Colors errorColor = raw_ostream::RED;
static const enum raw_ostream::Colors fatalColor = raw_ostream::RED;
// Used for changing only the bold attribute.
static const enum raw_ostream::Colors savedColor =
  raw_ostream::SAVEDCOLOR;

/// \brief Number of spaces to indent when word-wrapping.
const unsigned WordWrapIndentation = 6;

int bytesSincePreviousTabOrLineBegin(StringRef SourceLine, size_t i) {
  int bytes = 0;
  while (0<i) {
    if (SourceLine[--i]=='\t')
      break;
    ++bytes;
  }
  return bytes;
}

/// \brief returns a printable representation of first item from input range
///
/// This function returns a printable representation of the next item in a line
///  of source. If the next byte begins a valid and printable character, that
///  character is returned along with 'true'.
///
/// Otherwise, if the next byte begins a valid, but unprintable character, a
///  printable, escaped representation of the character is returned, along with
///  'false'. Otherwise a printable, escaped representation of the next byte
///  is returned along with 'false'.
///
/// \note The index is updated to be used with a subsequent call to
///        printableTextForNextCharacter.
///
/// \param SourceLine The line of source
/// \param i Pointer to byte index,
/// \param TabStop used to expand tabs
/// \return pair(printable text, 'true' iff original text was printable)
///
std::pair<SmallString<16>,bool>
printableTextForNextCharacter(StringRef SourceLine, size_t *i,
                              unsigned TabStop) {
  assert(i && "i must not be null");
  assert(*i<SourceLine.size() && "must point to a valid index");
  
  if (SourceLine[*i]=='\t') {
    assert(0 < TabStop && TabStop <= DiagnosticOptions::MaxTabStop &&
           "Invalid -ftabstop value");
    unsigned col = bytesSincePreviousTabOrLineBegin(SourceLine, *i);
    unsigned NumSpaces = TabStop - col%TabStop;
    assert(0 < NumSpaces && NumSpaces <= TabStop
           && "Invalid computation of space amt");
    ++(*i);

    SmallString<16> expandedTab;
    expandedTab.assign(NumSpaces, ' ');
    return std::make_pair(expandedTab, true);
  }

  // FIXME: this data is copied from the private implementation of ConvertUTF.h
  static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
  };

  unsigned char const *begin, *end;
  begin = reinterpret_cast<unsigned char const *>(&*(SourceLine.begin() + *i));
  end = begin + SourceLine.size();
  
  if (isLegalUTF8Sequence(begin, end)) {
    UTF32 c;
    UTF32 *cptr = &c;
    unsigned char const *original_begin = begin;
    char trailingBytes = trailingBytesForUTF8[(unsigned char)SourceLine[*i]];
    unsigned char const *cp_end = begin+trailingBytes+1;

    ConversionResult res = ConvertUTF8toUTF32(&begin, cp_end, &cptr, cptr+1,
                                              strictConversion);
    (void)res;
    assert(conversionOK==res);
    assert(0 < begin-original_begin
           && "we must be further along in the string now");
    *i += begin-original_begin;

    if (!llvm::sys::locale::isPrint(c)) {
      // If next character is valid UTF-8, but not printable
      SmallString<16> expandedCP("<U+>");
      while (c) {
        expandedCP.insert(expandedCP.begin()+3, llvm::hexdigit(c%16));
        c/=16;
      }
      while (expandedCP.size() < 8)
        expandedCP.insert(expandedCP.begin()+3, llvm::hexdigit(0));
      return std::make_pair(expandedCP, false);
    }

    // If next character is valid UTF-8, and printable
    return std::make_pair(SmallString<16>(original_begin, cp_end), true);

  }

  // If next byte is not valid UTF-8 (and therefore not printable)
  SmallString<16> expandedByte("<XX>");
  unsigned char byte = SourceLine[*i];
  expandedByte[1] = llvm::hexdigit(byte / 16);
  expandedByte[2] = llvm::hexdigit(byte % 16);
  ++(*i);
  return std::make_pair(expandedByte, false);
}

void expandTabs(std::string &SourceLine, unsigned TabStop) {
  size_t i = SourceLine.size();
  while (i>0) {
    i--;
    if (SourceLine[i]!='\t')
      continue;
    size_t tmp_i = i;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &tmp_i, TabStop);
    SourceLine.replace(i, 1, res.first.c_str());
  }
}

/// This function takes a raw source line and produces a mapping from the bytes
///  of the printable representation of the line to the columns those printable
///  characters will appear at (numbering the first column as 0).
///
/// If a byte 'i' corresponds to muliple columns (e.g. the byte contains a tab
///  character) then the the array will map that byte to the first column the
///  tab appears at and the next value in the map will have been incremented
///  more than once.
///
/// If a byte is the first in a sequence of bytes that together map to a single
///  entity in the output, then the array will map that byte to the appropriate
///  column while the subsequent bytes will be -1.
///
/// The last element in the array does not correspond to any byte in the input
///  and instead is the number of columns needed to display the source
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,8,9,-1,-1,11}
///
///  (\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
void byteToColumn(StringRef SourceLine, unsigned TabStop,
                         SmallVectorImpl<int> &out) {
  out.clear();

  if (SourceLine.empty()) {
    out.resize(1u,0);
    return;
  }
  
  out.resize(SourceLine.size()+1, -1);

  int columns = 0;
  size_t i = 0;
  while (i<SourceLine.size()) {
    out[i] = columns;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &i, TabStop);
    columns += llvm::sys::locale::columnWidth(res.first);
  }
  out.back() = columns;
}

/// This function takes a raw source line and produces a mapping from columns
///  to the byte of the source line that produced the character displaying at
///  that column. This is the inverse of the mapping produced by byteToColumn()
///
/// The last element in the array is the number of bytes in the source string
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,-1,-1,-1,-1,-1,3,4,-1,7}
///
///  (\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
void columnToByte(StringRef SourceLine, unsigned TabStop,
                         SmallVectorImpl<int> &out) {
  out.clear();

  if (SourceLine.empty()) {
    out.resize(1u, 0);
    return;
  }

  int columns = 0;
  size_t i = 0;
  while (i<SourceLine.size()) {
    out.resize(columns+1, -1);
    out.back() = i;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &i, TabStop);
    columns += llvm::sys::locale::columnWidth(res.first);
  }
  out.resize(columns+1, -1);
  out.back() = i;
}

struct SourceColumnMap {
  SourceColumnMap(StringRef SourceLine, unsigned TabStop)
  : m_SourceLine(SourceLine) {
    
    ::byteToColumn(SourceLine, TabStop, m_byteToColumn);
    ::columnToByte(SourceLine, TabStop, m_columnToByte);
    
    assert(m_byteToColumn.size()==SourceLine.size()+1);
    assert(0 < m_byteToColumn.size() && 0 < m_columnToByte.size());
    assert(m_byteToColumn.size()
           == static_cast<unsigned>(m_columnToByte.back()+1));
    assert(static_cast<unsigned>(m_byteToColumn.back()+1)
           == m_columnToByte.size());
  }
  int columns() const { return m_byteToColumn.back(); }
  int bytes() const { return m_columnToByte.back(); }
  int byteToColumn(int n) const {
    assert(0<=n && n<static_cast<int>(m_byteToColumn.size()));
    return m_byteToColumn[n];
  }
  int columnToByte(int n) const {
    assert(0<=n && n<static_cast<int>(m_columnToByte.size()));
    return m_columnToByte[n];
  }
  StringRef getSourceLine() const {
    return m_SourceLine;
  }
  
private:
  const std::string m_SourceLine;
  SmallVector<int,200> m_byteToColumn;
  SmallVector<int,200> m_columnToByte;
};

// used in assert in selectInterestingSourceRegion()
namespace {
struct char_out_of_range {
  const char lower,upper;
  char_out_of_range(char lower, char upper) :
    lower(lower), upper(upper) {}
  bool operator()(char c) { return c < lower || upper < c; }
};
}

/// \brief When the source code line we want to print is too long for
/// the terminal, select the "interesting" region.
static void selectInterestingSourceRegion(std::string &SourceLine,
                                          std::string &CaretLine,
                                          std::string &FixItInsertionLine,
                                          unsigned Columns,
                                          const SourceColumnMap &map) {
  unsigned MaxColumns = std::max<unsigned>(map.columns(),
                                           std::max(CaretLine.size(),
                                                    FixItInsertionLine.size()));
  // if the number of columns is less than the desired number we're done
  if (MaxColumns <= Columns)
    return;

  // no special characters allowed in CaretLine or FixItInsertionLine
  assert(CaretLine.end() ==
         std::find_if(CaretLine.begin(), CaretLine.end(),
         char_out_of_range(' ','~')));
  assert(FixItInsertionLine.end() ==
         std::find_if(FixItInsertionLine.begin(), FixItInsertionLine.end(),
         char_out_of_range(' ','~')));

  // Find the slice that we need to display the full caret line
  // correctly.
  unsigned CaretStart = 0, CaretEnd = CaretLine.size();
  for (; CaretStart != CaretEnd; ++CaretStart)
    if (!isspace(CaretLine[CaretStart]))
      break;

  for (; CaretEnd != CaretStart; --CaretEnd)
    if (!isspace(CaretLine[CaretEnd - 1]))
      break;

  // caret has already been inserted into CaretLine so the above whitespace
  // check is guaranteed to include the caret

  // If we have a fix-it line, make sure the slice includes all of the
  // fix-it information.
  if (!FixItInsertionLine.empty()) {
    unsigned FixItStart = 0, FixItEnd = FixItInsertionLine.size();
    for (; FixItStart != FixItEnd; ++FixItStart)
      if (!isspace(FixItInsertionLine[FixItStart]))
        break;

    for (; FixItEnd != FixItStart; --FixItEnd)
      if (!isspace(FixItInsertionLine[FixItEnd - 1]))
        break;

    CaretStart = std::min(FixItStart, CaretStart);
    CaretEnd = std::max(FixItEnd, CaretEnd);
  }

  // CaretLine[CaretStart, CaretEnd) contains all of the interesting
  // parts of the caret line. While this slice is smaller than the
  // number of columns we have, try to grow the slice to encompass
  // more context.

  unsigned SourceStart = map.columnToByte(std::min<unsigned>(CaretStart,
                                                             map.columns()));
  unsigned SourceEnd = map.columnToByte(std::min<unsigned>(CaretEnd,
                                                           map.columns()));

  unsigned CaretColumnsOutsideSource = CaretEnd-CaretStart
    - (map.byteToColumn(SourceEnd)-map.byteToColumn(SourceStart));

  char const *front_ellipse = "  ...";
  char const *front_space   = "     ";
  char const *back_ellipse = "...";
  unsigned ellipses_space = strlen(front_ellipse) + strlen(back_ellipse);

  unsigned TargetColumns = Columns;
  // Give us extra room for the ellipses
  //  and any of the caret line that extends past the source
  if (TargetColumns > ellipses_space+CaretColumnsOutsideSource)
    TargetColumns -= ellipses_space+CaretColumnsOutsideSource;

  while (SourceStart>0 || SourceEnd<SourceLine.size()) {
    bool ExpandedRegion = false;

    if (SourceStart>0) {
      unsigned NewStart = SourceStart-1;

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      while (NewStart &&
             (map.byteToColumn(NewStart)==-1 || isspace(SourceLine[NewStart])))
        --NewStart;

      // Skip over this bit of "interesting" text.
      while (NewStart &&
             (map.byteToColumn(NewStart)!=-1 && !isspace(SourceLine[NewStart])))
        --NewStart;

      // Move up to the non-whitespace character we just saw.
      if (NewStart)
        ++NewStart;

      unsigned NewColumns = map.byteToColumn(SourceEnd) -
                              map.byteToColumn(NewStart);
      if (NewColumns <= TargetColumns) {
        SourceStart = NewStart;
        ExpandedRegion = true;
      }
    }

    if (SourceEnd<SourceLine.size()) {
      unsigned NewEnd = SourceEnd+1;

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      while (NewEnd<SourceLine.size() &&
             (map.byteToColumn(NewEnd)==-1 || isspace(SourceLine[NewEnd])))
        ++NewEnd;

      // Skip over this bit of "interesting" text.
      while (NewEnd<SourceLine.size() &&
             (map.byteToColumn(NewEnd)!=-1 && !isspace(SourceLine[NewEnd])))
        ++NewEnd;

      unsigned NewColumns = map.byteToColumn(NewEnd) -
                              map.byteToColumn(SourceStart);
      if (NewColumns <= TargetColumns) {
        SourceEnd = NewEnd;
        ExpandedRegion = true;
      }
    }

    if (!ExpandedRegion)
      break;
  }

  CaretStart = map.byteToColumn(SourceStart);
  CaretEnd = map.byteToColumn(SourceEnd) + CaretColumnsOutsideSource;

  // [CaretStart, CaretEnd) is the slice we want. Update the various
  // output lines to show only this slice, with two-space padding
  // before the lines so that it looks nicer.

  assert(CaretStart!=(unsigned)-1 && CaretEnd!=(unsigned)-1 &&
         SourceStart!=(unsigned)-1 && SourceEnd!=(unsigned)-1);
  assert(SourceStart <= SourceEnd);
  assert(CaretStart <= CaretEnd);

  unsigned BackColumnsRemoved
    = map.byteToColumn(SourceLine.size())-map.byteToColumn(SourceEnd);
  unsigned FrontColumnsRemoved = CaretStart;
  unsigned ColumnsKept = CaretEnd-CaretStart;

  // We checked up front that the line needed truncation
  assert(FrontColumnsRemoved+ColumnsKept+BackColumnsRemoved > Columns);

  // The line needs some trunctiona, and we'd prefer to keep the front
  //  if possible, so remove the back
  if (BackColumnsRemoved)
    SourceLine.replace(SourceEnd, std::string::npos, back_ellipse);

  // If that's enough then we're done
  if (FrontColumnsRemoved+ColumnsKept <= Columns)
    return;

  // Otherwise remove the front as well
  if (FrontColumnsRemoved) {
    SourceLine.replace(0, SourceStart, front_ellipse);
    CaretLine.replace(0, CaretStart, front_space);
    if (!FixItInsertionLine.empty())
      FixItInsertionLine.replace(0, CaretStart, front_space);
  }
}

/// \brief Skip over whitespace in the string, starting at the given
/// index.
///
/// \returns The index of the first non-whitespace character that is
/// greater than or equal to Idx or, if no such character exists,
/// returns the end of the string.
static unsigned skipWhitespace(unsigned Idx, StringRef Str, unsigned Length) {
  while (Idx < Length && isspace(Str[Idx]))
    ++Idx;
  return Idx;
}

/// \brief If the given character is the start of some kind of
/// balanced punctuation (e.g., quotes or parentheses), return the
/// character that will terminate the punctuation.
///
/// \returns The ending punctuation character, if any, or the NULL
/// character if the input character does not start any punctuation.
static inline char findMatchingPunctuation(char c) {
  switch (c) {
  case '\'': return '\'';
  case '`': return '\'';
  case '"':  return '"';
  case '(':  return ')';
  case '[': return ']';
  case '{': return '}';
  default: break;
  }

  return 0;
}

/// \brief Find the end of the word starting at the given offset
/// within a string.
///
/// \returns the index pointing one character past the end of the
/// word.
static unsigned findEndOfWord(unsigned Start, StringRef Str,
                              unsigned Length, unsigned Column,
                              unsigned Columns) {
  assert(Start < Str.size() && "Invalid start position!");
  unsigned End = Start + 1;

  // If we are already at the end of the string, take that as the word.
  if (End == Str.size())
    return End;

  // Determine if the start of the string is actually opening
  // punctuation, e.g., a quote or parentheses.
  char EndPunct = findMatchingPunctuation(Str[Start]);
  if (!EndPunct) {
    // This is a normal word. Just find the first space character.
    while (End < Length && !isspace(Str[End]))
      ++End;
    return End;
  }

  // We have the start of a balanced punctuation sequence (quotes,
  // parentheses, etc.). Determine the full sequence is.
  SmallString<16> PunctuationEndStack;
  PunctuationEndStack.push_back(EndPunct);
  while (End < Length && !PunctuationEndStack.empty()) {
    if (Str[End] == PunctuationEndStack.back())
      PunctuationEndStack.pop_back();
    else if (char SubEndPunct = findMatchingPunctuation(Str[End]))
      PunctuationEndStack.push_back(SubEndPunct);

    ++End;
  }

  // Find the first space character after the punctuation ended.
  while (End < Length && !isspace(Str[End]))
    ++End;

  unsigned PunctWordLength = End - Start;
  if (// If the word fits on this line
      Column + PunctWordLength <= Columns ||
      // ... or the word is "short enough" to take up the next line
      // without too much ugly white space
      PunctWordLength < Columns/3)
    return End; // Take the whole thing as a single "word".

  // The whole quoted/parenthesized string is too long to print as a
  // single "word". Instead, find the "word" that starts just after
  // the punctuation and use that end-point instead. This will recurse
  // until it finds something small enough to consider a word.
  return findEndOfWord(Start + 1, Str, Length, Column + 1, Columns);
}

/// \brief Print the given string to a stream, word-wrapping it to
/// some number of columns in the process.
///
/// \param OS the stream to which the word-wrapping string will be
/// emitted.
/// \param Str the string to word-wrap and output.
/// \param Columns the number of columns to word-wrap to.
/// \param Column the column number at which the first character of \p
/// Str will be printed. This will be non-zero when part of the first
/// line has already been printed.
/// \param Indentation the number of spaces to indent any lines beyond
/// the first line.
/// \returns true if word-wrapping was required, or false if the
/// string fit on the first line.
static bool printWordWrapped(raw_ostream &OS, StringRef Str,
                             unsigned Columns,
                             unsigned Column = 0,
                             unsigned Indentation = WordWrapIndentation) {
  const unsigned Length = std::min(Str.find('\n'), Str.size());

  // The string used to indent each line.
  SmallString<16> IndentStr;
  IndentStr.assign(Indentation, ' ');
  bool Wrapped = false;
  for (unsigned WordStart = 0, WordEnd; WordStart < Length;
       WordStart = WordEnd) {
    // Find the beginning of the next word.
    WordStart = skipWhitespace(WordStart, Str, Length);
    if (WordStart == Length)
      break;

    // Find the end of this word.
    WordEnd = findEndOfWord(WordStart, Str, Length, Column, Columns);

    // Does this word fit on the current line?
    unsigned WordLength = WordEnd - WordStart;
    if (Column + WordLength < Columns) {
      // This word fits on the current line; print it there.
      if (WordStart) {
        OS << ' ';
        Column += 1;
      }
      OS << Str.substr(WordStart, WordLength);
      Column += WordLength;
      continue;
    }

    // This word does not fit on the current line, so wrap to the next
    // line.
    OS << '\n';
    OS.write(&IndentStr[0], Indentation);
    OS << Str.substr(WordStart, WordLength);
    Column = Indentation + WordLength;
    Wrapped = true;
  }

  // Append any remaning text from the message with its existing formatting.
  OS << Str.substr(Length);

  return Wrapped;
}

TextDiagnostic::TextDiagnostic(raw_ostream &OS,
                               const SourceManager &SM,
                               const LangOptions &LangOpts,
                               const DiagnosticOptions &DiagOpts)
  : DiagnosticRenderer(SM, LangOpts, DiagOpts), OS(OS) {}

TextDiagnostic::~TextDiagnostic() {}

void
TextDiagnostic::emitDiagnosticMessage(SourceLocation Loc,
                                      PresumedLoc PLoc,
                                      DiagnosticsEngine::Level Level,
                                      StringRef Message,
                                      ArrayRef<clang::CharSourceRange> Ranges,
                                      DiagOrStoredDiag D) {
  uint64_t StartOfLocationInfo = OS.tell();

  // Emit the location of this particular diagnostic.
  emitDiagnosticLoc(Loc, PLoc, Level, Ranges);
  
  if (DiagOpts.ShowColors)
    OS.resetColor();
  
  printDiagnosticLevel(OS, Level, DiagOpts.ShowColors);
  printDiagnosticMessage(OS, Level, Message,
                         OS.tell() - StartOfLocationInfo,
                         DiagOpts.MessageLength, DiagOpts.ShowColors);
}

/*static*/ void
TextDiagnostic::printDiagnosticLevel(raw_ostream &OS,
                                     DiagnosticsEngine::Level Level,
                                     bool ShowColors) {
  if (ShowColors) {
    // Print diagnostic category in bold and color
    switch (Level) {
    case DiagnosticsEngine::Ignored:
      llvm_unreachable("Invalid diagnostic type");
    case DiagnosticsEngine::Note:    OS.changeColor(noteColor, true); break;
    case DiagnosticsEngine::Warning: OS.changeColor(warningColor, true); break;
    case DiagnosticsEngine::Error:   OS.changeColor(errorColor, true); break;
    case DiagnosticsEngine::Fatal:   OS.changeColor(fatalColor, true); break;
    }
  }

  switch (Level) {
  case DiagnosticsEngine::Ignored:
    llvm_unreachable("Invalid diagnostic type");
  case DiagnosticsEngine::Note:    OS << "note: "; break;
  case DiagnosticsEngine::Warning: OS << "warning: "; break;
  case DiagnosticsEngine::Error:   OS << "error: "; break;
  case DiagnosticsEngine::Fatal:   OS << "fatal error: "; break;
  }

  if (ShowColors)
    OS.resetColor();
}

/*static*/ void
TextDiagnostic::printDiagnosticMessage(raw_ostream &OS,
                                       DiagnosticsEngine::Level Level,
                                       StringRef Message,
                                       unsigned CurrentColumn, unsigned Columns,
                                       bool ShowColors) {
  if (ShowColors) {
    // Print warnings, errors and fatal errors in bold, no color
    switch (Level) {
    case DiagnosticsEngine::Warning: OS.changeColor(savedColor, true); break;
    case DiagnosticsEngine::Error:   OS.changeColor(savedColor, true); break;
    case DiagnosticsEngine::Fatal:   OS.changeColor(savedColor, true); break;
    default: break; //don't bold notes
    }
  }

  if (Columns)
    printWordWrapped(OS, Message, Columns, CurrentColumn);
  else
    OS << Message;

  if (ShowColors)
    OS.resetColor();
  OS << '\n';
}

/// \brief Print out the file/line/column information and include trace.
///
/// This method handlen the emission of the diagnostic location information.
/// This includes extracting as much location information as is present for
/// the diagnostic and printing it, as well as any include stack or source
/// ranges necessary.
void TextDiagnostic::emitDiagnosticLoc(SourceLocation Loc, PresumedLoc PLoc,
                                       DiagnosticsEngine::Level Level,
                                       ArrayRef<CharSourceRange> Ranges) {
  if (PLoc.isInvalid()) {
    // At least print the file name if available:
    FileID FID = SM.getFileID(Loc);
    if (!FID.isInvalid()) {
      const FileEntry* FE = SM.getFileEntryForID(FID);
      if (FE && FE->getName()) {
        OS << FE->getName();
        if (FE->getDevice() == 0 && FE->getInode() == 0
            && FE->getFileMode() == 0) {
          // in PCH is a guess, but a good one:
          OS << " (in PCH)";
        }
        OS << ": ";
      }
    }
    return;
  }
  unsigned LineNo = PLoc.getLine();

  if (!DiagOpts.ShowLocation)
    return;

  if (DiagOpts.ShowColors)
    OS.changeColor(savedColor, true);

  OS << PLoc.getFilename();
  switch (DiagOpts.Format) {
  case DiagnosticOptions::Clang: OS << ':'  << LineNo; break;
  case DiagnosticOptions::Msvc:  OS << '('  << LineNo; break;
  case DiagnosticOptions::Vi:    OS << " +" << LineNo; break;
  }

  if (DiagOpts.ShowColumn)
    // Compute the column number.
    if (unsigned ColNo = PLoc.getColumn()) {
      if (DiagOpts.Format == DiagnosticOptions::Msvc) {
        OS << ',';
        ColNo--;
      } else
        OS << ':';
      OS << ColNo;
    }
  switch (DiagOpts.Format) {
  case DiagnosticOptions::Clang:
  case DiagnosticOptions::Vi:    OS << ':';    break;
  case DiagnosticOptions::Msvc:  OS << ") : "; break;
  }

  if (DiagOpts.ShowSourceRanges && !Ranges.empty()) {
    FileID CaretFileID =
      SM.getFileID(SM.getExpansionLoc(Loc));
    bool PrintedRange = false;

    for (ArrayRef<CharSourceRange>::const_iterator RI = Ranges.begin(),
         RE = Ranges.end();
         RI != RE; ++RI) {
      // Ignore invalid ranges.
      if (!RI->isValid()) continue;

      SourceLocation B = SM.getExpansionLoc(RI->getBegin());
      SourceLocation E = SM.getExpansionLoc(RI->getEnd());

      // If the End location and the start location are the same and are a
      // macro location, then the range was something that came from a
      // macro expansion or _Pragma.  If this is an object-like macro, the
      // best we can do is to highlight the range.  If this is a
      // function-like macro, we'd also like to highlight the arguments.
      if (B == E && RI->getEnd().isMacroID())
        E = SM.getExpansionRange(RI->getEnd()).second;

      std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(B);
      std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(E);

      // If the start or end of the range is in another file, just discard
      // it.
      if (BInfo.first != CaretFileID || EInfo.first != CaretFileID)
        continue;

      // Add in the length of the token, so that we cover multi-char
      // tokens.
      unsigned TokSize = 0;
      if (RI->isTokenRange())
        TokSize = Lexer::MeasureTokenLength(E, SM, LangOpts);

      OS << '{' << SM.getLineNumber(BInfo.first, BInfo.second) << ':'
        << SM.getColumnNumber(BInfo.first, BInfo.second) << '-'
        << SM.getLineNumber(EInfo.first, EInfo.second) << ':'
        << (SM.getColumnNumber(EInfo.first, EInfo.second)+TokSize)
        << '}';
      PrintedRange = true;
    }

    if (PrintedRange)
      OS << ':';
  }
  OS << ' ';
}

void TextDiagnostic::emitBasicNote(StringRef Message) {
  // FIXME: Emit this as a real note diagnostic.
  // FIXME: Format an actual diagnostic rather than a hard coded string.
  OS << "note: " << Message << "\n";
}

void TextDiagnostic::emitIncludeLocation(SourceLocation Loc,
                                         PresumedLoc PLoc) {
  if (DiagOpts.ShowLocation)
    OS << "In file included from " << PLoc.getFilename() << ':'
       << PLoc.getLine() << ":\n";
  else
    OS << "In included file:\n"; 
}

/// \brief Emit a code snippet and caret line.
///
/// This routine emits a single line's code snippet and caret line..
///
/// \param Loc The location for the caret.
/// \param Ranges The underlined ranges for this code snippet.
/// \param Hints The FixIt hints active for this diagnostic.
void TextDiagnostic::emitSnippetAndCaret(
    SourceLocation Loc, DiagnosticsEngine::Level Level,
    SmallVectorImpl<CharSourceRange>& Ranges,
    ArrayRef<FixItHint> Hints) {
  assert(!Loc.isInvalid() && "must have a valid source location here");
  assert(Loc.isFileID() && "must have a file location here");

  // If caret diagnostics are enabled and we have location, we want to
  // emit the caret.  However, we only do this if the location moved
  // from the last diagnostic, if the last diagnostic was a note that
  // was part of a different warning or error diagnostic, or if the
  // diagnostic has ranges.  We don't want to emit the same caret
  // multiple times if one loc has multiple diagnostics.
  if (!DiagOpts.ShowCarets)
    return;
  if (Loc == LastLoc && Ranges.empty() && Hints.empty() &&
      (LastLevel != DiagnosticsEngine::Note || Level == LastLevel))
    return;

  // Decompose the location into a FID/Offset pair.
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;

  // Get information about the buffer it points into.
  bool Invalid = false;
  StringRef BufData = SM.getBufferData(FID, &Invalid);
  if (Invalid)
    return;

  const char *BufStart = BufData.data();
  const char *BufEnd = BufStart + BufData.size();

  unsigned LineNo = SM.getLineNumber(FID, FileOffset);
  unsigned ColNo = SM.getColumnNumber(FID, FileOffset);
  unsigned CaretEndColNo
    = ColNo + Lexer::MeasureTokenLength(Loc, SM, LangOpts);

  // Rewind from the current position to the start of the line.
  const char *TokPtr = BufStart+FileOffset;
  const char *LineStart = TokPtr-ColNo+1; // Column # is 1-based.


  // Compute the line end.  Scan forward from the error position to the end of
  // the line.
  const char *LineEnd = TokPtr;
  while (*LineEnd != '\n' && *LineEnd != '\r' && LineEnd!=BufEnd)
    ++LineEnd;

  // FIXME: This shouldn't be necessary, but the CaretEndColNo can extend past
  // the source line length as currently being computed. See
  // test/Misc/message-length.c.
  CaretEndColNo = std::min(CaretEndColNo, unsigned(LineEnd - LineStart));

  // Copy the line of code into an std::string for ease of manipulation.
  std::string SourceLine(LineStart, LineEnd);

  // Create a line for the caret that is filled with spaces that is the same
  // length as the line of source code.
  std::string CaretLine(LineEnd-LineStart, ' ');

  const SourceColumnMap sourceColMap(SourceLine, DiagOpts.TabStop);

  // Highlight all of the characters covered by Ranges with ~ characters.
  for (SmallVectorImpl<CharSourceRange>::iterator I = Ranges.begin(),
                                                  E = Ranges.end();
       I != E; ++I)
    highlightRange(*I, LineNo, FID, sourceColMap, CaretLine);

  // Next, insert the caret itself.
  ColNo = sourceColMap.byteToColumn(ColNo-1);
  if (CaretLine.size()<ColNo+1)
    CaretLine.resize(ColNo+1, ' ');
  CaretLine[ColNo] = '^';

  std::string FixItInsertionLine = buildFixItInsertionLine(LineNo,
                                                           sourceColMap,
                                                           Hints);

  // If the source line is too long for our terminal, select only the
  // "interesting" source region within that line.
  unsigned Columns = DiagOpts.MessageLength;
  if (Columns)
    selectInterestingSourceRegion(SourceLine, CaretLine, FixItInsertionLine,
                                  Columns, sourceColMap);

  // If we are in -fdiagnostics-print-source-range-info mode, we are trying
  // to produce easily machine parsable output.  Add a space before the
  // source line and the caret to make it trivial to tell the main diagnostic
  // line from what the user is intended to see.
  if (DiagOpts.ShowSourceRanges) {
    SourceLine = ' ' + SourceLine;
    CaretLine = ' ' + CaretLine;
  }

  // Finally, remove any blank spaces from the end of CaretLine.
  while (CaretLine[CaretLine.size()-1] == ' ')
    CaretLine.erase(CaretLine.end()-1);

  // Emit what we have computed.
  emitSnippet(SourceLine);

  if (DiagOpts.ShowColors)
    OS.changeColor(caretColor, true);
  OS << CaretLine << '\n';
  if (DiagOpts.ShowColors)
    OS.resetColor();

  if (!FixItInsertionLine.empty()) {
    if (DiagOpts.ShowColors)
      // Print fixit line in color
      OS.changeColor(fixitColor, false);
    if (DiagOpts.ShowSourceRanges)
      OS << ' ';
    OS << FixItInsertionLine << '\n';
    if (DiagOpts.ShowColors)
      OS.resetColor();
  }

  // Print out any parseable fixit information requested by the options.
  emitParseableFixits(Hints);
}

void TextDiagnostic::emitSnippet(StringRef line)
{
  if (line.empty())
    return;

  size_t i = 0;
  
  std::string to_print;
  bool print_reversed = false;
  
  while (i<line.size()) {
    std::pair<SmallString<16>,bool> res
        = printableTextForNextCharacter(line, &i, DiagOpts.TabStop);
    bool was_printable = res.second;
    
    if (DiagOpts.ShowColors
        && was_printable==print_reversed) {
      if (print_reversed)
        OS.reverseColor();
      OS << to_print;
      to_print.clear();
      if (DiagOpts.ShowColors)
        OS.resetColor();
    }
    
    print_reversed = !was_printable;
    to_print += res.first.str();
  }
  
  if (print_reversed && DiagOpts.ShowColors)
    OS.reverseColor();
  OS << to_print;
  if (print_reversed && DiagOpts.ShowColors)
    OS.resetColor();
  
  OS << '\n';
}

/// \brief Highlight a SourceRange (with ~'s) for any characters on LineNo.
void TextDiagnostic::highlightRange(const CharSourceRange &R,
                                    unsigned LineNo, FileID FID,
                                    const SourceColumnMap &map,
                                    std::string &CaretLine) {
  if (!R.isValid()) return;

  SourceLocation Begin = SM.getExpansionLoc(R.getBegin());
  SourceLocation End = SM.getExpansionLoc(R.getEnd());

  // If the End location and the start location are the same and are a macro
  // location, then the range was something that came from a macro expansion
  // or _Pragma.  If this is an object-like macro, the best we can do is to
  // highlight the range.  If this is a function-like macro, we'd also like to
  // highlight the arguments.
  if (Begin == End && R.getEnd().isMacroID())
    End = SM.getExpansionRange(R.getEnd()).second;

  unsigned StartLineNo = SM.getExpansionLineNumber(Begin);
  if (StartLineNo > LineNo || SM.getFileID(Begin) != FID)
    return;  // No intersection.

  unsigned EndLineNo = SM.getExpansionLineNumber(End);
  if (EndLineNo < LineNo || SM.getFileID(End) != FID)
    return;  // No intersection.

  // Compute the column number of the start.
  unsigned StartColNo = 0;
  if (StartLineNo == LineNo) {
    StartColNo = SM.getExpansionColumnNumber(Begin);
    if (StartColNo) --StartColNo;  // Zero base the col #.
  }

  // Compute the column number of the end.
  unsigned EndColNo = map.getSourceLine().size();
  if (EndLineNo == LineNo) {
    EndColNo = SM.getExpansionColumnNumber(End);
    if (EndColNo) {
      --EndColNo;  // Zero base the col #.

      // Add in the length of the token, so that we cover multi-char tokens if
      // this is a token range.
      if (R.isTokenRange())
        EndColNo += Lexer::MeasureTokenLength(End, SM, LangOpts);
    } else {
      EndColNo = CaretLine.size();
    }
  }

  assert(StartColNo <= EndColNo && "Invalid range!");

  // Check that a token range does not highlight only whitespace.
  if (R.isTokenRange()) {
    // Pick the first non-whitespace column.
    while (StartColNo < map.getSourceLine().size() &&
           (map.getSourceLine()[StartColNo] == ' ' ||
            map.getSourceLine()[StartColNo] == '\t'))
      ++StartColNo;

    // Pick the last non-whitespace column.
    if (EndColNo > map.getSourceLine().size())
      EndColNo = map.getSourceLine().size();
    while (EndColNo-1 &&
           (map.getSourceLine()[EndColNo-1] == ' ' ||
            map.getSourceLine()[EndColNo-1] == '\t'))
      --EndColNo;

    // If the start/end passed each other, then we are trying to highlight a
    // range that just exists in whitespace, which must be some sort of other
    // bug.
    assert(StartColNo <= EndColNo && "Trying to highlight whitespace??");
  }

  assert(StartColNo <= map.getSourceLine().size() && "Invalid range!");
  assert(EndColNo <= map.getSourceLine().size() && "Invalid range!");

  // Fill the range with ~'s.
  StartColNo = map.byteToColumn(StartColNo);
  EndColNo = map.byteToColumn(EndColNo);

  assert(StartColNo <= EndColNo && "Invalid range!");
  if (CaretLine.size() < EndColNo)
    CaretLine.resize(EndColNo,' ');
  std::fill(CaretLine.begin()+StartColNo,CaretLine.begin()+EndColNo,'~');
}

std::string TextDiagnostic::buildFixItInsertionLine(
  unsigned LineNo,
  const SourceColumnMap &map,
  ArrayRef<FixItHint> Hints) {

  std::string FixItInsertionLine;
  if (Hints.empty() || !DiagOpts.ShowFixits)
    return FixItInsertionLine;

  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    if (!I->CodeToInsert.empty()) {
      // We have an insertion hint. Determine whether the inserted
      // code is on the same line as the caret.
      std::pair<FileID, unsigned> HintLocInfo
        = SM.getDecomposedExpansionLoc(I->RemoveRange.getBegin());
      if (LineNo == SM.getLineNumber(HintLocInfo.first, HintLocInfo.second)) {
        // Insert the new code into the line just below the code
        // that the user wrote.
        unsigned HintColNo
          = SM.getColumnNumber(HintLocInfo.first, HintLocInfo.second) - 1;
        // hint must start inside the source or right at the end
        assert(HintColNo<static_cast<unsigned>(map.bytes())+1);
        HintColNo = map.byteToColumn(HintColNo);

        // FIXME: if the fixit includes tabs or other characters that do not
        //  take up a single column per byte when displayed then
        //  I->CodeToInsert.size() is not a column number and we're mixing
        //  units (columns + bytes). We should get printable versions
        //  of each fixit before using them.
        unsigned LastColumnModified
          = HintColNo + I->CodeToInsert.size();

        if (LastColumnModified > static_cast<unsigned>(map.bytes())) {
          unsigned LastExistingColumn = map.byteToColumn(map.bytes());
          unsigned AddedColumns = LastColumnModified-LastExistingColumn;
          LastColumnModified = LastExistingColumn + AddedColumns;
        } else {
          LastColumnModified = map.byteToColumn(LastColumnModified);
        }

        if (LastColumnModified > FixItInsertionLine.size())
          FixItInsertionLine.resize(LastColumnModified, ' ');
        assert(HintColNo+I->CodeToInsert.size() <= FixItInsertionLine.size());
        std::copy(I->CodeToInsert.begin(), I->CodeToInsert.end(),
                  FixItInsertionLine.begin() + HintColNo);
      } else {
        FixItInsertionLine.clear();
        break;
      }
    }
  }

  expandTabs(FixItInsertionLine, DiagOpts.TabStop);

  return FixItInsertionLine;
}

void TextDiagnostic::emitParseableFixits(ArrayRef<FixItHint> Hints) {
  if (!DiagOpts.ShowParseableFixits)
    return;

  // We follow FixItRewriter's example in not (yet) handling
  // fix-its in macros.
  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    if (I->RemoveRange.isInvalid() ||
        I->RemoveRange.getBegin().isMacroID() ||
        I->RemoveRange.getEnd().isMacroID())
      return;
  }

  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    SourceLocation BLoc = I->RemoveRange.getBegin();
    SourceLocation ELoc = I->RemoveRange.getEnd();

    std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(BLoc);
    std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(ELoc);

    // Adjust for token ranges.
    if (I->RemoveRange.isTokenRange())
      EInfo.second += Lexer::MeasureTokenLength(ELoc, SM, LangOpts);

    // We specifically do not do word-wrapping or tab-expansion here,
    // because this is supposed to be easy to parse.
    PresumedLoc PLoc = SM.getPresumedLoc(BLoc);
    if (PLoc.isInvalid())
      break;

    OS << "fix-it:\"";
    OS.write_escaped(PLoc.getFilename());
    OS << "\":{" << SM.getLineNumber(BInfo.first, BInfo.second)
      << ':' << SM.getColumnNumber(BInfo.first, BInfo.second)
      << '-' << SM.getLineNumber(EInfo.first, EInfo.second)
      << ':' << SM.getColumnNumber(EInfo.first, EInfo.second)
      << "}:\"";
    OS.write_escaped(I->CodeToInsert);
    OS << "\"\n";
  }
}
