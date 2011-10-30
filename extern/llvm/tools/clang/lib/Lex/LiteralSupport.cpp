//===--- LiteralSupport.cpp - Code to parse and process literals ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the NumericLiteralParser, CharLiteralParser, and
// StringLiteralParser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;

/// HexDigitValue - Return the value of the specified hex digit, or -1 if it's
/// not valid.
static int HexDigitValue(char C) {
  if (C >= '0' && C <= '9') return C-'0';
  if (C >= 'a' && C <= 'f') return C-'a'+10;
  if (C >= 'A' && C <= 'F') return C-'A'+10;
  return -1;
}

static unsigned getCharWidth(tok::TokenKind kind, const TargetInfo &Target) {
  switch (kind) {
  default: llvm_unreachable("Unknown token type!");
  case tok::char_constant:
  case tok::string_literal:
  case tok::utf8_string_literal:
    return Target.getCharWidth();
  case tok::wide_char_constant:
  case tok::wide_string_literal:
    return Target.getWCharWidth();
  case tok::utf16_char_constant:
  case tok::utf16_string_literal:
    return Target.getChar16Width();
  case tok::utf32_char_constant:
  case tok::utf32_string_literal:
    return Target.getChar32Width();
  }
}

/// ProcessCharEscape - Parse a standard C escape sequence, which can occur in
/// either a character or a string literal.
static unsigned ProcessCharEscape(const char *&ThisTokBuf,
                                  const char *ThisTokEnd, bool &HadError,
                                  FullSourceLoc Loc, unsigned CharWidth,
                                  DiagnosticsEngine *Diags) {
  // Skip the '\' char.
  ++ThisTokBuf;

  // We know that this character can't be off the end of the buffer, because
  // that would have been \", which would not have been the end of string.
  unsigned ResultChar = *ThisTokBuf++;
  switch (ResultChar) {
  // These map to themselves.
  case '\\': case '\'': case '"': case '?': break;

    // These have fixed mappings.
  case 'a':
    // TODO: K&R: the meaning of '\\a' is different in traditional C
    ResultChar = 7;
    break;
  case 'b':
    ResultChar = 8;
    break;
  case 'e':
    if (Diags)
      Diags->Report(Loc, diag::ext_nonstandard_escape) << "e";
    ResultChar = 27;
    break;
  case 'E':
    if (Diags)
      Diags->Report(Loc, diag::ext_nonstandard_escape) << "E";
    ResultChar = 27;
    break;
  case 'f':
    ResultChar = 12;
    break;
  case 'n':
    ResultChar = 10;
    break;
  case 'r':
    ResultChar = 13;
    break;
  case 't':
    ResultChar = 9;
    break;
  case 'v':
    ResultChar = 11;
    break;
  case 'x': { // Hex escape.
    ResultChar = 0;
    if (ThisTokBuf == ThisTokEnd || !isxdigit(*ThisTokBuf)) {
      if (Diags)
        Diags->Report(Loc, diag::err_hex_escape_no_digits);
      HadError = 1;
      break;
    }

    // Hex escapes are a maximal series of hex digits.
    bool Overflow = false;
    for (; ThisTokBuf != ThisTokEnd; ++ThisTokBuf) {
      int CharVal = HexDigitValue(ThisTokBuf[0]);
      if (CharVal == -1) break;
      // About to shift out a digit?
      Overflow |= (ResultChar & 0xF0000000) ? true : false;
      ResultChar <<= 4;
      ResultChar |= CharVal;
    }

    // See if any bits will be truncated when evaluated as a character.
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      Overflow = true;
      ResultChar &= ~0U >> (32-CharWidth);
    }

    // Check for overflow.
    if (Overflow && Diags)   // Too many digits to fit in
      Diags->Report(Loc, diag::warn_hex_escape_too_large);
    break;
  }
  case '0': case '1': case '2': case '3':
  case '4': case '5': case '6': case '7': {
    // Octal escapes.
    --ThisTokBuf;
    ResultChar = 0;

    // Octal escapes are a series of octal digits with maximum length 3.
    // "\0123" is a two digit sequence equal to "\012" "3".
    unsigned NumDigits = 0;
    do {
      ResultChar <<= 3;
      ResultChar |= *ThisTokBuf++ - '0';
      ++NumDigits;
    } while (ThisTokBuf != ThisTokEnd && NumDigits < 3 &&
             ThisTokBuf[0] >= '0' && ThisTokBuf[0] <= '7');

    // Check for overflow.  Reject '\777', but not L'\777'.
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      if (Diags)
        Diags->Report(Loc, diag::warn_octal_escape_too_large);
      ResultChar &= ~0U >> (32-CharWidth);
    }
    break;
  }

    // Otherwise, these are not valid escapes.
  case '(': case '{': case '[': case '%':
    // GCC accepts these as extensions.  We warn about them as such though.
    if (Diags)
      Diags->Report(Loc, diag::ext_nonstandard_escape)
        << std::string()+(char)ResultChar;
    break;
  default:
    if (Diags == 0)
      break;
      
    if (isgraph(ResultChar))
      Diags->Report(Loc, diag::ext_unknown_escape)
        << std::string()+(char)ResultChar;
    else
      Diags->Report(Loc, diag::ext_unknown_escape)
        << "x"+llvm::utohexstr(ResultChar);
    break;
  }

  return ResultChar;
}

/// ProcessUCNEscape - Read the Universal Character Name, check constraints and
/// return the UTF32.
static bool ProcessUCNEscape(const char *&ThisTokBuf, const char *ThisTokEnd,
                             uint32_t &UcnVal, unsigned short &UcnLen,
                             FullSourceLoc Loc, DiagnosticsEngine *Diags, 
                             const LangOptions &Features) {
  if (!Features.CPlusPlus && !Features.C99 && Diags)
    Diags->Report(Loc, diag::warn_ucn_not_valid_in_c89);

  // Save the beginning of the string (for error diagnostics).
  const char *ThisTokBegin = ThisTokBuf;

  // Skip the '\u' char's.
  ThisTokBuf += 2;

  if (ThisTokBuf == ThisTokEnd || !isxdigit(*ThisTokBuf)) {
    if (Diags)
      Diags->Report(Loc, diag::err_ucn_escape_no_digits);
    return false;
  }
  UcnLen = (ThisTokBuf[-1] == 'u' ? 4 : 8);
  unsigned short UcnLenSave = UcnLen;
  for (; ThisTokBuf != ThisTokEnd && UcnLenSave; ++ThisTokBuf, UcnLenSave--) {
    int CharVal = HexDigitValue(ThisTokBuf[0]);
    if (CharVal == -1) break;
    UcnVal <<= 4;
    UcnVal |= CharVal;
  }
  // If we didn't consume the proper number of digits, there is a problem.
  if (UcnLenSave) {
    if (Diags) {
      SourceLocation L =
        Lexer::AdvanceToTokenCharacter(Loc, ThisTokBuf-ThisTokBegin,
                                       Loc.getManager(), Features);
      Diags->Report(FullSourceLoc(L, Loc.getManager()),
                    diag::err_ucn_escape_incomplete);
    }
    return false;
  }
  // Check UCN constraints (C99 6.4.3p2).
  if ((UcnVal < 0xa0 &&
      (UcnVal != 0x24 && UcnVal != 0x40 && UcnVal != 0x60 )) // $, @, `
      || (UcnVal >= 0xD800 && UcnVal <= 0xDFFF)
      || (UcnVal > 0x10FFFF)) /* the maximum legal UTF32 value */ {
    if (Diags)
      Diags->Report(Loc, diag::err_ucn_escape_invalid);
    return false;
  }
  return true;
}

/// EncodeUCNEscape - Read the Universal Character Name, check constraints and
/// convert the UTF32 to UTF8 or UTF16. This is a subroutine of
/// StringLiteralParser. When we decide to implement UCN's for identifiers,
/// we will likely rework our support for UCN's.
static void EncodeUCNEscape(const char *&ThisTokBuf, const char *ThisTokEnd,
                            char *&ResultBuf, bool &HadError,
                            FullSourceLoc Loc, unsigned CharByteWidth,
                            DiagnosticsEngine *Diags,
                            const LangOptions &Features) {
  typedef uint32_t UTF32;
  UTF32 UcnVal = 0;
  unsigned short UcnLen = 0;
  if (!ProcessUCNEscape(ThisTokBuf, ThisTokEnd, UcnVal, UcnLen, Loc, Diags,
                        Features)) {
    HadError = 1;
    return;
  }

  assert((CharByteWidth == 1 || CharByteWidth == 2 || CharByteWidth) &&
         "only character widths of 1, 2, or 4 bytes supported");

  (void)UcnLen;
  assert((UcnLen== 4 || UcnLen== 8) && "only ucn length of 4 or 8 supported");

  if (CharByteWidth == 4) {
    // Note: our internal rep of wide char tokens is always little-endian.
    *ResultBuf++ = (UcnVal & 0x000000FF);
    *ResultBuf++ = (UcnVal & 0x0000FF00) >> 8;
    *ResultBuf++ = (UcnVal & 0x00FF0000) >> 16;
    *ResultBuf++ = (UcnVal & 0xFF000000) >> 24;
    return;
  }

  if (CharByteWidth == 2) {
    // Convert to UTF16.
    if (UcnVal < (UTF32)0xFFFF) {
      *ResultBuf++ = (UcnVal & 0x000000FF);
      *ResultBuf++ = (UcnVal & 0x0000FF00) >> 8;
      return;
    }
    if (Diags) Diags->Report(Loc, diag::warn_ucn_escape_too_large);

    typedef uint16_t UTF16;
    UcnVal -= 0x10000;
    UTF16 surrogate1 = 0xD800 + (UcnVal >> 10);
    UTF16 surrogate2 = 0xDC00 + (UcnVal & 0x3FF);
    *ResultBuf++ = (surrogate1 & 0x000000FF);
    *ResultBuf++ = (surrogate1 & 0x0000FF00) >> 8;
    *ResultBuf++ = (surrogate2 & 0x000000FF);
    *ResultBuf++ = (surrogate2 & 0x0000FF00) >> 8;
    return;
  }

  assert(CharByteWidth == 1 && "UTF-8 encoding is only for 1 byte characters");

  // Now that we've parsed/checked the UCN, we convert from UTF32->UTF8.
  // The conversion below was inspired by:
  //   http://www.unicode.org/Public/PROGRAMS/CVTUTF/ConvertUTF.c
  // First, we determine how many bytes the result will require.
  typedef uint8_t UTF8;

  unsigned short bytesToWrite = 0;
  if (UcnVal < (UTF32)0x80)
    bytesToWrite = 1;
  else if (UcnVal < (UTF32)0x800)
    bytesToWrite = 2;
  else if (UcnVal < (UTF32)0x10000)
    bytesToWrite = 3;
  else
    bytesToWrite = 4;

  const unsigned byteMask = 0xBF;
  const unsigned byteMark = 0x80;

  // Once the bits are split out into bytes of UTF8, this is a mask OR-ed
  // into the first byte, depending on how many bytes follow.
  static const UTF8 firstByteMark[5] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0
  };
  // Finally, we write the bytes into ResultBuf.
  ResultBuf += bytesToWrite;
  switch (bytesToWrite) { // note: everything falls through.
    case 4: *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    case 3: *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    case 2: *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    case 1: *--ResultBuf = (UTF8) (UcnVal | firstByteMark[bytesToWrite]);
  }
  // Update the buffer.
  ResultBuf += bytesToWrite;
}


///       integer-constant: [C99 6.4.4.1]
///         decimal-constant integer-suffix
///         octal-constant integer-suffix
///         hexadecimal-constant integer-suffix
///       decimal-constant:
///         nonzero-digit
///         decimal-constant digit
///       octal-constant:
///         0
///         octal-constant octal-digit
///       hexadecimal-constant:
///         hexadecimal-prefix hexadecimal-digit
///         hexadecimal-constant hexadecimal-digit
///       hexadecimal-prefix: one of
///         0x 0X
///       integer-suffix:
///         unsigned-suffix [long-suffix]
///         unsigned-suffix [long-long-suffix]
///         long-suffix [unsigned-suffix]
///         long-long-suffix [unsigned-sufix]
///       nonzero-digit:
///         1 2 3 4 5 6 7 8 9
///       octal-digit:
///         0 1 2 3 4 5 6 7
///       hexadecimal-digit:
///         0 1 2 3 4 5 6 7 8 9
///         a b c d e f
///         A B C D E F
///       unsigned-suffix: one of
///         u U
///       long-suffix: one of
///         l L
///       long-long-suffix: one of
///         ll LL
///
///       floating-constant: [C99 6.4.4.2]
///         TODO: add rules...
///
NumericLiteralParser::
NumericLiteralParser(const char *begin, const char *end,
                     SourceLocation TokLoc, Preprocessor &pp)
  : PP(pp), ThisTokBegin(begin), ThisTokEnd(end) {

  // This routine assumes that the range begin/end matches the regex for integer
  // and FP constants (specifically, the 'pp-number' regex), and assumes that
  // the byte at "*end" is both valid and not part of the regex.  Because of
  // this, it doesn't have to check for 'overscan' in various places.
  assert(!isalnum(*end) && *end != '.' && *end != '_' &&
         "Lexer didn't maximally munch?");

  s = DigitsBegin = begin;
  saw_exponent = false;
  saw_period = false;
  isLong = false;
  isUnsigned = false;
  isLongLong = false;
  isFloat = false;
  isImaginary = false;
  isMicrosoftInteger = false;
  hadError = false;

  if (*s == '0') { // parse radix
    ParseNumberStartingWithZero(TokLoc);
    if (hadError)
      return;
  } else { // the first digit is non-zero
    radix = 10;
    s = SkipDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (isxdigit(*s) && !(*s == 'e' || *s == 'E')) {
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-begin),
              diag::err_invalid_decimal_digit) << StringRef(s, 1);
      hadError = true;
      return;
    } else if (*s == '.') {
      s++;
      saw_period = true;
      s = SkipDigits(s);
    }
    if ((*s == 'e' || *s == 'E')) { // exponent
      const char *Exponent = s;
      s++;
      saw_exponent = true;
      if (*s == '+' || *s == '-')  s++; // sign
      const char *first_non_digit = SkipDigits(s);
      if (first_non_digit != s) {
        s = first_non_digit;
      } else {
        PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, Exponent-begin),
                diag::err_exponent_has_no_digits);
        hadError = true;
        return;
      }
    }
  }

  SuffixBegin = s;

  // Parse the suffix.  At this point we can classify whether we have an FP or
  // integer constant.
  bool isFPConstant = isFloatingLiteral();

  // Loop over all of the characters of the suffix.  If we see something bad,
  // we break out of the loop.
  for (; s != ThisTokEnd; ++s) {
    switch (*s) {
    case 'f':      // FP Suffix for "float"
    case 'F':
      if (!isFPConstant) break;  // Error for integer constant.
      if (isFloat || isLong) break; // FF, LF invalid.
      isFloat = true;
      continue;  // Success.
    case 'u':
    case 'U':
      if (isFPConstant) break;  // Error for floating constant.
      if (isUnsigned) break;    // Cannot be repeated.
      isUnsigned = true;
      continue;  // Success.
    case 'l':
    case 'L':
      if (isLong || isLongLong) break;  // Cannot be repeated.
      if (isFloat) break;               // LF invalid.

      // Check for long long.  The L's need to be adjacent and the same case.
      if (s+1 != ThisTokEnd && s[1] == s[0]) {
        if (isFPConstant) break;        // long long invalid for floats.
        isLongLong = true;
        ++s;  // Eat both of them.
      } else {
        isLong = true;
      }
      continue;  // Success.
    case 'i':
    case 'I':
      if (PP.getLangOptions().MicrosoftExt) {
        if (isFPConstant || isLong || isLongLong) break;

        // Allow i8, i16, i32, i64, and i128.
        if (s + 1 != ThisTokEnd) {
          switch (s[1]) {
            case '8':
              s += 2; // i8 suffix
              isMicrosoftInteger = true;
              break;
            case '1':
              if (s + 2 == ThisTokEnd) break;
              if (s[2] == '6') {
                s += 3; // i16 suffix
                isMicrosoftInteger = true;
              }
              else if (s[2] == '2') {
                if (s + 3 == ThisTokEnd) break;
                if (s[3] == '8') {
                  s += 4; // i128 suffix
                  isMicrosoftInteger = true;
                }
              }
              break;
            case '3':
              if (s + 2 == ThisTokEnd) break;
              if (s[2] == '2') {
                s += 3; // i32 suffix
                isLong = true;
                isMicrosoftInteger = true;
              }
              break;
            case '6':
              if (s + 2 == ThisTokEnd) break;
              if (s[2] == '4') {
                s += 3; // i64 suffix
                isLongLong = true;
                isMicrosoftInteger = true;
              }
              break;
            default:
              break;
          }
          break;
        }
      }
      // fall through.
    case 'j':
    case 'J':
      if (isImaginary) break;   // Cannot be repeated.
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-begin),
              diag::ext_imaginary_constant);
      isImaginary = true;
      continue;  // Success.
    }
    // If we reached here, there was an error.
    break;
  }

  // Report an error if there are any.
  if (s != ThisTokEnd) {
    PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-begin),
            isFPConstant ? diag::err_invalid_suffix_float_constant :
                           diag::err_invalid_suffix_integer_constant)
      << StringRef(SuffixBegin, ThisTokEnd-SuffixBegin);
    hadError = true;
    return;
  }
}

/// ParseNumberStartingWithZero - This method is called when the first character
/// of the number is found to be a zero.  This means it is either an octal
/// number (like '04') or a hex number ('0x123a') a binary number ('0b1010') or
/// a floating point number (01239.123e4).  Eat the prefix, determining the
/// radix etc.
void NumericLiteralParser::ParseNumberStartingWithZero(SourceLocation TokLoc) {
  assert(s[0] == '0' && "Invalid method call");
  s++;

  // Handle a hex number like 0x1234.
  if ((*s == 'x' || *s == 'X') && (isxdigit(s[1]) || s[1] == '.')) {
    s++;
    radix = 16;
    DigitsBegin = s;
    s = SkipHexDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (*s == '.') {
      s++;
      saw_period = true;
      s = SkipHexDigits(s);
    }
    // A binary exponent can appear with or with a '.'. If dotted, the
    // binary exponent is required.
    if (*s == 'p' || *s == 'P') {
      const char *Exponent = s;
      s++;
      saw_exponent = true;
      if (*s == '+' || *s == '-')  s++; // sign
      const char *first_non_digit = SkipDigits(s);
      if (first_non_digit == s) {
        PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, Exponent-ThisTokBegin),
                diag::err_exponent_has_no_digits);
        hadError = true;
        return;
      }
      s = first_non_digit;

      if (!PP.getLangOptions().HexFloats)
        PP.Diag(TokLoc, diag::ext_hexconstant_invalid);
    } else if (saw_period) {
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-ThisTokBegin),
              diag::err_hexconstant_requires_exponent);
      hadError = true;
    }
    return;
  }

  // Handle simple binary numbers 0b01010
  if (*s == 'b' || *s == 'B') {
    // 0b101010 is a GCC extension.
    PP.Diag(TokLoc, diag::ext_binary_literal);
    ++s;
    radix = 2;
    DigitsBegin = s;
    s = SkipBinaryDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (isxdigit(*s)) {
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-ThisTokBegin),
              diag::err_invalid_binary_digit) << StringRef(s, 1);
      hadError = true;
    }
    // Other suffixes will be diagnosed by the caller.
    return;
  }

  // For now, the radix is set to 8. If we discover that we have a
  // floating point constant, the radix will change to 10. Octal floating
  // point constants are not permitted (only decimal and hexadecimal).
  radix = 8;
  DigitsBegin = s;
  s = SkipOctalDigits(s);
  if (s == ThisTokEnd)
    return; // Done, simple octal number like 01234

  // If we have some other non-octal digit that *is* a decimal digit, see if
  // this is part of a floating point number like 094.123 or 09e1.
  if (isdigit(*s)) {
    const char *EndDecimal = SkipDigits(s);
    if (EndDecimal[0] == '.' || EndDecimal[0] == 'e' || EndDecimal[0] == 'E') {
      s = EndDecimal;
      radix = 10;
    }
  }

  // If we have a hex digit other than 'e' (which denotes a FP exponent) then
  // the code is using an incorrect base.
  if (isxdigit(*s) && *s != 'e' && *s != 'E') {
    PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-ThisTokBegin),
            diag::err_invalid_octal_digit) << StringRef(s, 1);
    hadError = true;
    return;
  }

  if (*s == '.') {
    s++;
    radix = 10;
    saw_period = true;
    s = SkipDigits(s); // Skip suffix.
  }
  if (*s == 'e' || *s == 'E') { // exponent
    const char *Exponent = s;
    s++;
    radix = 10;
    saw_exponent = true;
    if (*s == '+' || *s == '-')  s++; // sign
    const char *first_non_digit = SkipDigits(s);
    if (first_non_digit != s) {
      s = first_non_digit;
    } else {
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, Exponent-ThisTokBegin),
              diag::err_exponent_has_no_digits);
      hadError = true;
      return;
    }
  }
}


/// GetIntegerValue - Convert this numeric literal value to an APInt that
/// matches Val's input width.  If there is an overflow, set Val to the low bits
/// of the result and return true.  Otherwise, return false.
bool NumericLiteralParser::GetIntegerValue(llvm::APInt &Val) {
  // Fast path: Compute a conservative bound on the maximum number of
  // bits per digit in this radix. If we can't possibly overflow a
  // uint64 based on that bound then do the simple conversion to
  // integer. This avoids the expensive overflow checking below, and
  // handles the common cases that matter (small decimal integers and
  // hex/octal values which don't overflow).
  unsigned MaxBitsPerDigit = 1;
  while ((1U << MaxBitsPerDigit) < radix)
    MaxBitsPerDigit += 1;
  if ((SuffixBegin - DigitsBegin) * MaxBitsPerDigit <= 64) {
    uint64_t N = 0;
    for (s = DigitsBegin; s != SuffixBegin; ++s)
      N = N*radix + HexDigitValue(*s);

    // This will truncate the value to Val's input width. Simply check
    // for overflow by comparing.
    Val = N;
    return Val.getZExtValue() != N;
  }

  Val = 0;
  s = DigitsBegin;

  llvm::APInt RadixVal(Val.getBitWidth(), radix);
  llvm::APInt CharVal(Val.getBitWidth(), 0);
  llvm::APInt OldVal = Val;

  bool OverflowOccurred = false;
  while (s < SuffixBegin) {
    unsigned C = HexDigitValue(*s++);

    // If this letter is out of bound for this radix, reject it.
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");

    CharVal = C;

    // Add the digit to the value in the appropriate radix.  If adding in digits
    // made the value smaller, then this overflowed.
    OldVal = Val;

    // Multiply by radix, did overflow occur on the multiply?
    Val *= RadixVal;
    OverflowOccurred |= Val.udiv(RadixVal) != OldVal;

    // Add value, did overflow occur on the value?
    //   (a + b) ult b  <=> overflow
    Val += CharVal;
    OverflowOccurred |= Val.ult(CharVal);
  }
  return OverflowOccurred;
}

llvm::APFloat::opStatus
NumericLiteralParser::GetFloatValue(llvm::APFloat &Result) {
  using llvm::APFloat;

  unsigned n = std::min(SuffixBegin - ThisTokBegin, ThisTokEnd - ThisTokBegin);
  return Result.convertFromString(StringRef(ThisTokBegin, n),
                                  APFloat::rmNearestTiesToEven);
}


///       character-literal: [C++0x lex.ccon]
///         ' c-char-sequence '
///         u' c-char-sequence '
///         U' c-char-sequence '
///         L' c-char-sequence '
///       c-char-sequence:
///         c-char
///         c-char-sequence c-char
///       c-char:
///         any member of the source character set except the single-quote ',
///           backslash \, or new-line character
///         escape-sequence
///         universal-character-name
///       escape-sequence: [C++0x lex.ccon]
///         simple-escape-sequence
///         octal-escape-sequence
///         hexadecimal-escape-sequence
///       simple-escape-sequence:
///         one of \' \" \? \\ \a \b \f \n \r \t \v
///       octal-escape-sequence:
///         \ octal-digit
///         \ octal-digit octal-digit
///         \ octal-digit octal-digit octal-digit
///       hexadecimal-escape-sequence:
///         \x hexadecimal-digit
///         hexadecimal-escape-sequence hexadecimal-digit
///       universal-character-name:
///         \u hex-quad
///         \U hex-quad hex-quad
///       hex-quad:
///         hex-digit hex-digit hex-digit hex-digit
///
CharLiteralParser::CharLiteralParser(const char *begin, const char *end,
                                     SourceLocation Loc, Preprocessor &PP,
                                     tok::TokenKind kind) {
  // At this point we know that the character matches the regex "L?'.*'".
  HadError = false;

  Kind = kind;

  // Determine if this is a wide or UTF character.
  if (Kind == tok::wide_char_constant || Kind == tok::utf16_char_constant ||
      Kind == tok::utf32_char_constant) {
    ++begin;
  }

  // Skip over the entry quote.
  assert(begin[0] == '\'' && "Invalid token lexed");
  ++begin;

  // FIXME: The "Value" is an uint64_t so we can handle char literals of
  // up to 64-bits.
  // FIXME: This extensively assumes that 'char' is 8-bits.
  assert(PP.getTargetInfo().getCharWidth() == 8 &&
         "Assumes char is 8 bits");
  assert(PP.getTargetInfo().getIntWidth() <= 64 &&
         (PP.getTargetInfo().getIntWidth() & 7) == 0 &&
         "Assumes sizeof(int) on target is <= 64 and a multiple of char");
  assert(PP.getTargetInfo().getWCharWidth() <= 64 &&
         "Assumes sizeof(wchar) on target is <= 64");

  // This is what we will use for overflow detection
  llvm::APInt LitVal(PP.getTargetInfo().getIntWidth(), 0);

  unsigned NumCharsSoFar = 0;
  bool Warned = false;
  while (begin[0] != '\'') {
    uint64_t ResultChar;

      // Is this a Universal Character Name escape?
    if (begin[0] != '\\')     // If this is a normal character, consume it.
      ResultChar = (unsigned char)*begin++;
    else {                    // Otherwise, this is an escape character.
      unsigned CharWidth = getCharWidth(Kind, PP.getTargetInfo());
      // Check for UCN.
      if (begin[1] == 'u' || begin[1] == 'U') {
        uint32_t utf32 = 0;
        unsigned short UcnLen = 0;
        if (!ProcessUCNEscape(begin, end, utf32, UcnLen,
                              FullSourceLoc(Loc, PP.getSourceManager()),
                              &PP.getDiagnostics(), PP.getLangOptions())) {
          HadError = 1;
        }
        ResultChar = utf32;
        if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
          PP.Diag(Loc, diag::warn_ucn_escape_too_large);
          ResultChar &= ~0U >> (32-CharWidth);
        }
      } else {
        // Otherwise, this is a non-UCN escape character.  Process it.
        ResultChar = ProcessCharEscape(begin, end, HadError,
                                       FullSourceLoc(Loc,PP.getSourceManager()),
                                       CharWidth, &PP.getDiagnostics());
      }
    }

    // If this is a multi-character constant (e.g. 'abc'), handle it.  These are
    // implementation defined (C99 6.4.4.4p10).
    if (NumCharsSoFar) {
      if (!isAscii()) {
        // Emulate GCC's (unintentional?) behavior: L'ab' -> L'b'.
        LitVal = 0;
      } else {
        // Narrow character literals act as though their value is concatenated
        // in this implementation, but warn on overflow.
        if (LitVal.countLeadingZeros() < 8 && !Warned) {
          PP.Diag(Loc, diag::warn_char_constant_too_large);
          Warned = true;
        }
        LitVal <<= 8;
      }
    }

    LitVal = LitVal + ResultChar;
    ++NumCharsSoFar;
  }

  // If this is the second character being processed, do special handling.
  if (NumCharsSoFar > 1) {
    // Warn about discarding the top bits for multi-char wide-character
    // constants (L'abcd').
    if (!isAscii())
      PP.Diag(Loc, diag::warn_extraneous_char_constant);
    else if (NumCharsSoFar != 4)
      PP.Diag(Loc, diag::ext_multichar_character_literal);
    else
      PP.Diag(Loc, diag::ext_four_char_character_literal);
    IsMultiChar = true;
  } else
    IsMultiChar = false;

  // Transfer the value from APInt to uint64_t
  Value = LitVal.getZExtValue();

  // If this is a single narrow character, sign extend it (e.g. '\xFF' is "-1")
  // if 'char' is signed for this target (C99 6.4.4.4p10).  Note that multiple
  // character constants are not sign extended in the this implementation:
  // '\xFF\xFF' = 65536 and '\x0\xFF' = 255, which matches GCC.
  if (isAscii() && NumCharsSoFar == 1 && (Value & 128) &&
      PP.getLangOptions().CharIsSigned)
    Value = (signed char)Value;
}


///       string-literal: [C++0x lex.string]
///         encoding-prefix " [s-char-sequence] "
///         encoding-prefix R raw-string
///       encoding-prefix:
///         u8
///         u
///         U
///         L
///       s-char-sequence:
///         s-char
///         s-char-sequence s-char
///       s-char:
///         any member of the source character set except the double-quote ",
///           backslash \, or new-line character
///         escape-sequence
///         universal-character-name
///       raw-string:
///         " d-char-sequence ( r-char-sequence ) d-char-sequence "
///       r-char-sequence:
///         r-char
///         r-char-sequence r-char
///       r-char:
///         any member of the source character set, except a right parenthesis )
///           followed by the initial d-char-sequence (which may be empty)
///           followed by a double quote ".
///       d-char-sequence:
///         d-char
///         d-char-sequence d-char
///       d-char:
///         any member of the basic source character set except:
///           space, the left parenthesis (, the right parenthesis ),
///           the backslash \, and the control characters representing horizontal
///           tab, vertical tab, form feed, and newline.
///       escape-sequence: [C++0x lex.ccon]
///         simple-escape-sequence
///         octal-escape-sequence
///         hexadecimal-escape-sequence
///       simple-escape-sequence:
///         one of \' \" \? \\ \a \b \f \n \r \t \v
///       octal-escape-sequence:
///         \ octal-digit
///         \ octal-digit octal-digit
///         \ octal-digit octal-digit octal-digit
///       hexadecimal-escape-sequence:
///         \x hexadecimal-digit
///         hexadecimal-escape-sequence hexadecimal-digit
///       universal-character-name:
///         \u hex-quad
///         \U hex-quad hex-quad
///       hex-quad:
///         hex-digit hex-digit hex-digit hex-digit
///
StringLiteralParser::
StringLiteralParser(const Token *StringToks, unsigned NumStringToks,
                    Preprocessor &PP, bool Complain)
  : SM(PP.getSourceManager()), Features(PP.getLangOptions()),
    Target(PP.getTargetInfo()), Diags(Complain ? &PP.getDiagnostics() : 0),
    MaxTokenLength(0), SizeBound(0), CharByteWidth(0), Kind(tok::unknown),
    ResultPtr(ResultBuf.data()), hadError(false), Pascal(false) {
  init(StringToks, NumStringToks);
}

void StringLiteralParser::init(const Token *StringToks, unsigned NumStringToks){
  // The literal token may have come from an invalid source location (e.g. due
  // to a PCH error), in which case the token length will be 0.
  if (NumStringToks == 0 || StringToks[0].getLength() < 2) {
    hadError = true;
    return;
  }

  // Scan all of the string portions, remember the max individual token length,
  // computing a bound on the concatenated string length, and see whether any
  // piece is a wide-string.  If any of the string portions is a wide-string
  // literal, the result is a wide-string literal [C99 6.4.5p4].
  assert(NumStringToks && "expected at least one token");
  MaxTokenLength = StringToks[0].getLength();
  assert(StringToks[0].getLength() >= 2 && "literal token is invalid!");
  SizeBound = StringToks[0].getLength()-2;  // -2 for "".
  Kind = StringToks[0].getKind();

  hadError = false;

  // Implement Translation Phase #6: concatenation of string literals
  /// (C99 5.1.1.2p1).  The common case is only one string fragment.
  for (unsigned i = 1; i != NumStringToks; ++i) {
    if (StringToks[i].getLength() < 2) {
      hadError = true;
      return;
    }

    // The string could be shorter than this if it needs cleaning, but this is a
    // reasonable bound, which is all we need.
    assert(StringToks[i].getLength() >= 2 && "literal token is invalid!");
    SizeBound += StringToks[i].getLength()-2;  // -2 for "".

    // Remember maximum string piece length.
    if (StringToks[i].getLength() > MaxTokenLength)
      MaxTokenLength = StringToks[i].getLength();

    // Remember if we see any wide or utf-8/16/32 strings.
    // Also check for illegal concatenations.
    if (StringToks[i].isNot(Kind) && StringToks[i].isNot(tok::string_literal)) {
      if (isAscii()) {
        Kind = StringToks[i].getKind();
      } else {
        if (Diags)
          Diags->Report(FullSourceLoc(StringToks[i].getLocation(), SM),
                        diag::err_unsupported_string_concat);
        hadError = true;
      }
    }
  }

  // Include space for the null terminator.
  ++SizeBound;

  // TODO: K&R warning: "traditional C rejects string constant concatenation"

  // Get the width in bytes of char/wchar_t/char16_t/char32_t
  CharByteWidth = getCharWidth(Kind, Target);
  assert((CharByteWidth & 7) == 0 && "Assumes character size is byte multiple");
  CharByteWidth /= 8;

  // The output buffer size needs to be large enough to hold wide characters.
  // This is a worst-case assumption which basically corresponds to L"" "long".
  SizeBound *= CharByteWidth;

  // Size the temporary buffer to hold the result string data.
  ResultBuf.resize(SizeBound);

  // Likewise, but for each string piece.
  llvm::SmallString<512> TokenBuf;
  TokenBuf.resize(MaxTokenLength);

  // Loop over all the strings, getting their spelling, and expanding them to
  // wide strings as appropriate.
  ResultPtr = &ResultBuf[0];   // Next byte to fill in.

  Pascal = false;

  for (unsigned i = 0, e = NumStringToks; i != e; ++i) {
    const char *ThisTokBuf = &TokenBuf[0];
    // Get the spelling of the token, which eliminates trigraphs, etc.  We know
    // that ThisTokBuf points to a buffer that is big enough for the whole token
    // and 'spelled' tokens can only shrink.
    bool StringInvalid = false;
    unsigned ThisTokLen = 
      Lexer::getSpelling(StringToks[i], ThisTokBuf, SM, Features,
                         &StringInvalid);
    if (StringInvalid) {
      hadError = true;
      continue;
    }

    const char *ThisTokEnd = ThisTokBuf+ThisTokLen-1;  // Skip end quote.
    // TODO: Input character set mapping support.

    // Skip marker for wide or unicode strings.
    if (ThisTokBuf[0] == 'L' || ThisTokBuf[0] == 'u' || ThisTokBuf[0] == 'U') {
      ++ThisTokBuf;
      // Skip 8 of u8 marker for utf8 strings.
      if (ThisTokBuf[0] == '8')
        ++ThisTokBuf;
    }

    // Check for raw string
    if (ThisTokBuf[0] == 'R') {
      ThisTokBuf += 2; // skip R"

      const char *Prefix = ThisTokBuf;
      while (ThisTokBuf[0] != '(')
        ++ThisTokBuf;
      ++ThisTokBuf; // skip '('

      // remove same number of characters from the end
      if (ThisTokEnd >= ThisTokBuf + (ThisTokBuf - Prefix))
        ThisTokEnd -= (ThisTokBuf - Prefix);

      // Copy the string over
      CopyStringFragment(StringRef(ThisTokBuf, ThisTokEnd - ThisTokBuf));
    } else {
      assert(ThisTokBuf[0] == '"' && "Expected quote, lexer broken?");
      ++ThisTokBuf; // skip "

      // Check if this is a pascal string
      if (Features.PascalStrings && ThisTokBuf + 1 != ThisTokEnd &&
          ThisTokBuf[0] == '\\' && ThisTokBuf[1] == 'p') {

        // If the \p sequence is found in the first token, we have a pascal string
        // Otherwise, if we already have a pascal string, ignore the first \p
        if (i == 0) {
          ++ThisTokBuf;
          Pascal = true;
        } else if (Pascal)
          ThisTokBuf += 2;
      }

      while (ThisTokBuf != ThisTokEnd) {
        // Is this a span of non-escape characters?
        if (ThisTokBuf[0] != '\\') {
          const char *InStart = ThisTokBuf;
          do {
            ++ThisTokBuf;
          } while (ThisTokBuf != ThisTokEnd && ThisTokBuf[0] != '\\');

          // Copy the character span over.
          CopyStringFragment(StringRef(InStart, ThisTokBuf - InStart));
          continue;
        }
        // Is this a Universal Character Name escape?
        if (ThisTokBuf[1] == 'u' || ThisTokBuf[1] == 'U') {
          EncodeUCNEscape(ThisTokBuf, ThisTokEnd, ResultPtr,
                          hadError, FullSourceLoc(StringToks[i].getLocation(),SM),
                          CharByteWidth, Diags, Features);
          continue;
        }
        // Otherwise, this is a non-UCN escape character.  Process it.
        unsigned ResultChar =
          ProcessCharEscape(ThisTokBuf, ThisTokEnd, hadError,
                            FullSourceLoc(StringToks[i].getLocation(), SM),
                            CharByteWidth*8, Diags);

        // Note: our internal rep of wide char tokens is always little-endian.
        *ResultPtr++ = ResultChar & 0xFF;

        for (unsigned i = 1, e = CharByteWidth; i != e; ++i)
          *ResultPtr++ = ResultChar >> i*8;
      }
    }
  }

  if (Pascal) {
    ResultBuf[0] = ResultPtr-&ResultBuf[0]-1;
    ResultBuf[0] /= CharByteWidth;

    // Verify that pascal strings aren't too large.
    if (GetStringLength() > 256) {
      if (Diags) 
        Diags->Report(FullSourceLoc(StringToks[0].getLocation(), SM),
                      diag::err_pascal_string_too_long)
          << SourceRange(StringToks[0].getLocation(),
                         StringToks[NumStringToks-1].getLocation());
      hadError = true;
      return;
    }
  } else if (Diags) {
    // Complain if this string literal has too many characters.
    unsigned MaxChars = Features.CPlusPlus? 65536 : Features.C99 ? 4095 : 509;
    
    if (GetNumStringChars() > MaxChars)
      Diags->Report(FullSourceLoc(StringToks[0].getLocation(), SM),
                    diag::ext_string_too_long)
        << GetNumStringChars() << MaxChars
        << (Features.CPlusPlus ? 2 : Features.C99 ? 1 : 0)
        << SourceRange(StringToks[0].getLocation(),
                       StringToks[NumStringToks-1].getLocation());
  }
}


/// copyStringFragment - This function copies from Start to End into ResultPtr.
/// Performs widening for multi-byte characters.
void StringLiteralParser::CopyStringFragment(StringRef Fragment) {
  // Copy the character span over.
  if (CharByteWidth == 1) {
    memcpy(ResultPtr, Fragment.data(), Fragment.size());
    ResultPtr += Fragment.size();
  } else {
    // Note: our internal rep of wide char tokens is always little-endian.
    for (StringRef::iterator I=Fragment.begin(), E=Fragment.end(); I!=E; ++I) {
      *ResultPtr++ = *I;
      // Add zeros at the end.
      for (unsigned i = 1, e = CharByteWidth; i != e; ++i)
        *ResultPtr++ = 0;
    }
  }
}


/// getOffsetOfStringByte - This function returns the offset of the
/// specified byte of the string data represented by Token.  This handles
/// advancing over escape sequences in the string.
unsigned StringLiteralParser::getOffsetOfStringByte(const Token &Tok,
                                                    unsigned ByteNo) const {
  // Get the spelling of the token.
  llvm::SmallString<32> SpellingBuffer;
  SpellingBuffer.resize(Tok.getLength());

  bool StringInvalid = false;
  const char *SpellingPtr = &SpellingBuffer[0];
  unsigned TokLen = Lexer::getSpelling(Tok, SpellingPtr, SM, Features,
                                       &StringInvalid);
  if (StringInvalid)
    return 0;

  assert(SpellingPtr[0] != 'L' && SpellingPtr[0] != 'u' &&
         SpellingPtr[0] != 'U' && "Doesn't handle wide or utf strings yet");


  const char *SpellingStart = SpellingPtr;
  const char *SpellingEnd = SpellingPtr+TokLen;

  // Skip over the leading quote.
  assert(SpellingPtr[0] == '"' && "Should be a string literal!");
  ++SpellingPtr;

  // Skip over bytes until we find the offset we're looking for.
  while (ByteNo) {
    assert(SpellingPtr < SpellingEnd && "Didn't find byte offset!");

    // Step over non-escapes simply.
    if (*SpellingPtr != '\\') {
      ++SpellingPtr;
      --ByteNo;
      continue;
    }

    // Otherwise, this is an escape character.  Advance over it.
    bool HadError = false;
    ProcessCharEscape(SpellingPtr, SpellingEnd, HadError,
                      FullSourceLoc(Tok.getLocation(), SM),
                      CharByteWidth*8, Diags);
    assert(!HadError && "This method isn't valid on erroneous strings");
    --ByteNo;
  }

  return SpellingPtr-SpellingStart;
}
