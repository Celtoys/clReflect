//= FormatString.h - Analysis of printf/fprintf format strings --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines APIs for analyzing the format strings of printf, fscanf,
// and friends.
//
// The structure of format strings for fprintf are described in C99 7.19.6.1.
//
// The structure of format strings for fscanf are described in C99 7.19.6.2.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FORMAT_H
#define LLVM_CLANG_FORMAT_H

#include "clang/AST/CanonicalType.h"

namespace clang {

//===----------------------------------------------------------------------===//
/// Common components of both fprintf and fscanf format strings.
namespace analyze_format_string {

/// Class representing optional flags with location and representation
/// information.
class OptionalFlag {
public:
  OptionalFlag(const char *Representation)
      : representation(Representation), flag(false) {}
  bool isSet() { return flag; }
  void set() { flag = true; }
  void clear() { flag = false; }
  void setPosition(const char *position) {
    assert(position);
    this->position = position;
  }
  const char *getPosition() const {
    assert(position);
    return position;
  }
  const char *toString() const { return representation; }

  // Overloaded operators for bool like qualities
  operator bool() const { return flag; }
  OptionalFlag& operator=(const bool &rhs) {
    flag = rhs;
    return *this;  // Return a reference to myself.
  }
private:
  const char *representation;
  const char *position;
  bool flag;
};

/// Represents the length modifier in a format string in scanf/printf.
class LengthModifier {
public:
  enum Kind {
    None,
    AsChar,       // 'hh'
    AsShort,      // 'h'
    AsLong,       // 'l'
    AsLongLong,   // 'll'
    AsQuad,       // 'q' (BSD, deprecated, for 64-bit integer types)
    AsIntMax,     // 'j'
    AsSizeT,      // 'z'
    AsPtrDiff,    // 't'
    AsLongDouble, // 'L'
    AsAllocate,   // for '%as', GNU extension to C90 scanf
    AsMAllocate,  // for '%ms', GNU extension to scanf
    AsWideChar = AsLong // for '%ls', only makes sense for printf
  };

  LengthModifier()
    : Position(0), kind(None) {}
  LengthModifier(const char *pos, Kind k)
    : Position(pos), kind(k) {}

  const char *getStart() const {
    return Position;
  }

  unsigned getLength() const {
    switch (kind) {
      default:
        return 1;
      case AsLongLong:
      case AsChar:
        return 2;
      case None:
        return 0;
    }
  }

  Kind getKind() const { return kind; }
  void setKind(Kind k) { kind = k; }

  const char *toString() const;

private:
  const char *Position;
  Kind kind;
};

class ConversionSpecifier {
public:
  enum Kind {
    InvalidSpecifier = 0,
      // C99 conversion specifiers.
    cArg,
    dArg,
    iArg,
    IntArgBeg = cArg, IntArgEnd = iArg,

    oArg,
    uArg,
    xArg,
    XArg,
    UIntArgBeg = oArg, UIntArgEnd = XArg,

    fArg,
    FArg,
    eArg,
    EArg,
    gArg,
    GArg,
    aArg,
    AArg,
    DoubleArgBeg = fArg, DoubleArgEnd = AArg,

    sArg,
    pArg,
    nArg,
    PercentArg,
    CArg,
    SArg,

    // ** Printf-specific **

    // Objective-C specific specifiers.
    ObjCObjArg,  // '@'
    ObjCBeg = ObjCObjArg, ObjCEnd = ObjCObjArg,

    // GlibC specific specifiers.
    PrintErrno,   // 'm'

    PrintfConvBeg = ObjCObjArg, PrintfConvEnd = PrintErrno,

    // ** Scanf-specific **
    ScanListArg, // '['
    ScanfConvBeg = ScanListArg, ScanfConvEnd = ScanListArg
  };

  ConversionSpecifier(bool isPrintf)
    : IsPrintf(isPrintf), Position(0), EndScanList(0), kind(InvalidSpecifier) {}

  ConversionSpecifier(bool isPrintf, const char *pos, Kind k)
    : IsPrintf(isPrintf), Position(pos), EndScanList(0), kind(k) {}

  const char *getStart() const {
    return Position;
  }

  StringRef getCharacters() const {
    return StringRef(getStart(), getLength());
  }

  bool consumesDataArgument() const {
    switch (kind) {
      case PrintErrno:
        assert(IsPrintf);
      case PercentArg:
        return false;
      default:
        return true;
    }
  }

  Kind getKind() const { return kind; }
  void setKind(Kind k) { kind = k; }
  unsigned getLength() const {
    return EndScanList ? EndScanList - Position : 1;
  }

  bool isUIntArg() const { return kind >= UIntArgBeg && kind <= UIntArgEnd; }
  const char *toString() const;

  bool isPrintfKind() const { return IsPrintf; }

protected:
  bool IsPrintf;
  const char *Position;
  const char *EndScanList;
  Kind kind;
};

class ArgTypeResult {
public:
  enum Kind { UnknownTy, InvalidTy, SpecificTy, ObjCPointerTy, CPointerTy,
              AnyCharTy, CStrTy, WCStrTy, WIntTy };
private:
  const Kind K;
  QualType T;
  const char *Name;
  ArgTypeResult(bool) : K(InvalidTy), Name(0) {}
public:
  ArgTypeResult(Kind k = UnknownTy) : K(k), Name(0) {}
  ArgTypeResult(Kind k, const char *n) : K(k), Name(n) {}
  ArgTypeResult(QualType t) : K(SpecificTy), T(t), Name(0) {}
  ArgTypeResult(QualType t, const char *n) : K(SpecificTy), T(t), Name(n)  {}
  ArgTypeResult(CanQualType t) : K(SpecificTy), T(t), Name(0) {}

  static ArgTypeResult Invalid() { return ArgTypeResult(true); }

  bool isValid() const { return K != InvalidTy; }

  const QualType *getSpecificType() const {
    return K == SpecificTy ? &T : 0;
  }

  bool matchesType(ASTContext &C, QualType argTy) const;

  bool matchesAnyObjCObjectRef() const { return K == ObjCPointerTy; }

  QualType getRepresentativeType(ASTContext &C) const;

  std::string getRepresentativeTypeName(ASTContext &C) const;
};

class OptionalAmount {
public:
  enum HowSpecified { NotSpecified, Constant, Arg, Invalid };

  OptionalAmount(HowSpecified howSpecified,
                 unsigned amount,
                 const char *amountStart,
                 unsigned amountLength,
                 bool usesPositionalArg)
  : start(amountStart), length(amountLength), hs(howSpecified), amt(amount),
  UsesPositionalArg(usesPositionalArg), UsesDotPrefix(0) {}

  OptionalAmount(bool valid = true)
  : start(0),length(0), hs(valid ? NotSpecified : Invalid), amt(0),
  UsesPositionalArg(0), UsesDotPrefix(0) {}

  bool isInvalid() const {
    return hs == Invalid;
  }

  HowSpecified getHowSpecified() const { return hs; }
  void setHowSpecified(HowSpecified h) { hs = h; }

  bool hasDataArgument() const { return hs == Arg; }

  unsigned getArgIndex() const {
    assert(hasDataArgument());
    return amt;
  }

  unsigned getConstantAmount() const {
    assert(hs == Constant);
    return amt;
  }

  const char *getStart() const {
      // We include the . character if it is given.
    return start - UsesDotPrefix;
  }

  unsigned getConstantLength() const {
    assert(hs == Constant);
    return length + UsesDotPrefix;
  }

  ArgTypeResult getArgType(ASTContext &Ctx) const;

  void toString(raw_ostream &os) const;

  bool usesPositionalArg() const { return (bool) UsesPositionalArg; }
  unsigned getPositionalArgIndex() const {
    assert(hasDataArgument());
    return amt + 1;
  }

  bool usesDotPrefix() const { return UsesDotPrefix; }
  void setUsesDotPrefix() { UsesDotPrefix = true; }

private:
  const char *start;
  unsigned length;
  HowSpecified hs;
  unsigned amt;
  bool UsesPositionalArg : 1;
  bool UsesDotPrefix;
};


class FormatSpecifier {
protected:
  LengthModifier LM;
  OptionalAmount FieldWidth;
  ConversionSpecifier CS;
  /// Positional arguments, an IEEE extension:
  ///  IEEE Std 1003.1, 2004 Edition
  ///  http://www.opengroup.org/onlinepubs/009695399/functions/printf.html
  bool UsesPositionalArg;
  unsigned argIndex;
public:
  FormatSpecifier(bool isPrintf)
    : CS(isPrintf), UsesPositionalArg(false), argIndex(0) {}

  void setLengthModifier(LengthModifier lm) {
    LM = lm;
  }

  void setUsesPositionalArg() { UsesPositionalArg = true; }

  void setArgIndex(unsigned i) {
    argIndex = i;
  }

  unsigned getArgIndex() const {
    return argIndex;
  }

  unsigned getPositionalArgIndex() const {
    return argIndex + 1;
  }

  const LengthModifier &getLengthModifier() const {
    return LM;
  }

  const OptionalAmount &getFieldWidth() const {
    return FieldWidth;
  }

  void setFieldWidth(const OptionalAmount &Amt) {
    FieldWidth = Amt;
  }

  bool usesPositionalArg() const { return UsesPositionalArg; }

  bool hasValidLengthModifier() const;

  bool hasStandardLengthModifier() const;

  bool hasStandardConversionSpecifier(const LangOptions &LangOpt) const;

  bool hasStandardLengthConversionCombination() const;
};

} // end analyze_format_string namespace

//===----------------------------------------------------------------------===//
/// Pieces specific to fprintf format strings.

namespace analyze_printf {

class PrintfConversionSpecifier :
  public analyze_format_string::ConversionSpecifier  {
public:
  PrintfConversionSpecifier()
    : ConversionSpecifier(true, 0, InvalidSpecifier) {}

  PrintfConversionSpecifier(const char *pos, Kind k)
    : ConversionSpecifier(true, pos, k) {}

  bool isObjCArg() const { return kind >= ObjCBeg && kind <= ObjCEnd; }
  bool isIntArg() const { return kind >= IntArgBeg && kind <= IntArgEnd; }
  bool isDoubleArg() const { return kind >= DoubleArgBeg &&
                                    kind <= DoubleArgEnd; }
  unsigned getLength() const {
      // Conversion specifiers currently only are represented by
      // single characters, but we be flexible.
    return 1;
  }

  static bool classof(const analyze_format_string::ConversionSpecifier *CS) {
    return CS->isPrintfKind();
  }
};

using analyze_format_string::ArgTypeResult;
using analyze_format_string::LengthModifier;
using analyze_format_string::OptionalAmount;
using analyze_format_string::OptionalFlag;

class PrintfSpecifier : public analyze_format_string::FormatSpecifier {
  OptionalFlag HasThousandsGrouping; // ''', POSIX extension.
  OptionalFlag IsLeftJustified; // '-'
  OptionalFlag HasPlusPrefix; // '+'
  OptionalFlag HasSpacePrefix; // ' '
  OptionalFlag HasAlternativeForm; // '#'
  OptionalFlag HasLeadingZeroes; // '0'
  OptionalAmount Precision;
public:
  PrintfSpecifier() :
    FormatSpecifier(/* isPrintf = */ true),
    HasThousandsGrouping("'"), IsLeftJustified("-"), HasPlusPrefix("+"),
    HasSpacePrefix(" "), HasAlternativeForm("#"), HasLeadingZeroes("0") {}

  static PrintfSpecifier Parse(const char *beg, const char *end);

    // Methods for incrementally constructing the PrintfSpecifier.
  void setConversionSpecifier(const PrintfConversionSpecifier &cs) {
    CS = cs;
  }
  void setHasThousandsGrouping(const char *position) {
    HasThousandsGrouping = true;
    HasThousandsGrouping.setPosition(position);
  }
  void setIsLeftJustified(const char *position) {
    IsLeftJustified = true;
    IsLeftJustified.setPosition(position);
  }
  void setHasPlusPrefix(const char *position) {
    HasPlusPrefix = true;
    HasPlusPrefix.setPosition(position);
  }
  void setHasSpacePrefix(const char *position) {
    HasSpacePrefix = true;
    HasSpacePrefix.setPosition(position);
  }
  void setHasAlternativeForm(const char *position) {
    HasAlternativeForm = true;
    HasAlternativeForm.setPosition(position);
  }
  void setHasLeadingZeros(const char *position) {
    HasLeadingZeroes = true;
    HasLeadingZeroes.setPosition(position);
  }
  void setUsesPositionalArg() { UsesPositionalArg = true; }

    // Methods for querying the format specifier.

  const PrintfConversionSpecifier &getConversionSpecifier() const {
    return cast<PrintfConversionSpecifier>(CS);
  }

  void setPrecision(const OptionalAmount &Amt) {
    Precision = Amt;
    Precision.setUsesDotPrefix();
  }

  const OptionalAmount &getPrecision() const {
    return Precision;
  }

  bool consumesDataArgument() const {
    return getConversionSpecifier().consumesDataArgument();
  }

  /// \brief Returns the builtin type that a data argument
  /// paired with this format specifier should have.  This method
  /// will return null if the format specifier does not have
  /// a matching data argument or the matching argument matches
  /// more than one type.
  ArgTypeResult getArgType(ASTContext &Ctx, bool IsObjCLiteral) const;

  const OptionalFlag &hasThousandsGrouping() const {
      return HasThousandsGrouping;
  }
  const OptionalFlag &isLeftJustified() const { return IsLeftJustified; }
  const OptionalFlag &hasPlusPrefix() const { return HasPlusPrefix; }
  const OptionalFlag &hasAlternativeForm() const { return HasAlternativeForm; }
  const OptionalFlag &hasLeadingZeros() const { return HasLeadingZeroes; }
  const OptionalFlag &hasSpacePrefix() const { return HasSpacePrefix; }
  bool usesPositionalArg() const { return UsesPositionalArg; }

  /// Changes the specifier and length according to a QualType, retaining any
  /// flags or options. Returns true on success, or false when a conversion
  /// was not successful.
  bool fixType(QualType QT, const LangOptions &LangOpt, ASTContext &Ctx,
               bool IsObjCLiteral);

  void toString(raw_ostream &os) const;

  // Validation methods - to check if any element results in undefined behavior
  bool hasValidPlusPrefix() const;
  bool hasValidAlternativeForm() const;
  bool hasValidLeadingZeros() const;
  bool hasValidSpacePrefix() const;
  bool hasValidLeftJustified() const;
  bool hasValidThousandsGroupingPrefix() const;

  bool hasValidPrecision() const;
  bool hasValidFieldWidth() const;
};
}  // end analyze_printf namespace

//===----------------------------------------------------------------------===//
/// Pieces specific to fscanf format strings.

namespace analyze_scanf {

class ScanfConversionSpecifier :
    public analyze_format_string::ConversionSpecifier  {
public:
  ScanfConversionSpecifier()
    : ConversionSpecifier(false, 0, InvalidSpecifier) {}

  ScanfConversionSpecifier(const char *pos, Kind k)
    : ConversionSpecifier(false, pos, k) {}

  void setEndScanList(const char *pos) { EndScanList = pos; }

  static bool classof(const analyze_format_string::ConversionSpecifier *CS) {
    return !CS->isPrintfKind();
  }
};

using analyze_format_string::ArgTypeResult;
using analyze_format_string::LengthModifier;
using analyze_format_string::OptionalAmount;
using analyze_format_string::OptionalFlag;

class ScanfArgTypeResult : public ArgTypeResult {
public:
  enum Kind { UnknownTy, InvalidTy, CStrTy, WCStrTy, PtrToArgTypeResultTy };
private:
  Kind K;
  ArgTypeResult A;
  const char *Name;
  QualType getRepresentativeType(ASTContext &C) const;
public:
  ScanfArgTypeResult(Kind k = UnknownTy, const char* n = 0) : K(k), Name(n) {}
  ScanfArgTypeResult(ArgTypeResult a, const char *n = 0)
      : K(PtrToArgTypeResultTy), A(a), Name(n) {
    assert(A.isValid());
  }

  static ScanfArgTypeResult Invalid() { return ScanfArgTypeResult(InvalidTy); }

  bool isValid() const { return K != InvalidTy; }

  bool matchesType(ASTContext& C, QualType argTy) const;

  std::string getRepresentativeTypeName(ASTContext& C) const;
};

class ScanfSpecifier : public analyze_format_string::FormatSpecifier {
  OptionalFlag SuppressAssignment; // '*'
public:
  ScanfSpecifier() :
    FormatSpecifier(/* isPrintf = */ false),
    SuppressAssignment("*") {}

  void setSuppressAssignment(const char *position) {
    SuppressAssignment = true;
    SuppressAssignment.setPosition(position);
  }

  const OptionalFlag &getSuppressAssignment() const {
    return SuppressAssignment;
  }

  void setConversionSpecifier(const ScanfConversionSpecifier &cs) {
    CS = cs;
  }

  const ScanfConversionSpecifier &getConversionSpecifier() const {
    return cast<ScanfConversionSpecifier>(CS);
  }

  bool consumesDataArgument() const {
    return CS.consumesDataArgument() && !SuppressAssignment;
  }

  ScanfArgTypeResult getArgType(ASTContext &Ctx) const;

  bool fixType(QualType QT, const LangOptions &LangOpt, ASTContext &Ctx);

  void toString(raw_ostream &os) const;

  static ScanfSpecifier Parse(const char *beg, const char *end);
};

} // end analyze_scanf namespace

//===----------------------------------------------------------------------===//
// Parsing and processing of format strings (both fprintf and fscanf).

namespace analyze_format_string {

enum PositionContext { FieldWidthPos = 0, PrecisionPos = 1 };

class FormatStringHandler {
public:
  FormatStringHandler() {}
  virtual ~FormatStringHandler();

  virtual void HandleNullChar(const char *nullCharacter) {}

  virtual void HandlePosition(const char *startPos, unsigned posLen) {}

  virtual void HandleInvalidPosition(const char *startPos, unsigned posLen,
                                     PositionContext p) {}

  virtual void HandleZeroPosition(const char *startPos, unsigned posLen) {}

  virtual void HandleIncompleteSpecifier(const char *startSpecifier,
                                         unsigned specifierLen) {}

  // Printf-specific handlers.

  virtual bool HandleInvalidPrintfConversionSpecifier(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const char *startSpecifier,
                                      unsigned specifierLen) {
    return true;
  }

  virtual bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                                     const char *startSpecifier,
                                     unsigned specifierLen) {
    return true;
  }

    // Scanf-specific handlers.

  virtual bool HandleInvalidScanfConversionSpecifier(
                                        const analyze_scanf::ScanfSpecifier &FS,
                                        const char *startSpecifier,
                                        unsigned specifierLen) {
    return true;
  }

  virtual bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                                    const char *startSpecifier,
                                    unsigned specifierLen) {
    return true;
  }

  virtual void HandleIncompleteScanList(const char *start, const char *end) {}
};

bool ParsePrintfString(FormatStringHandler &H,
                       const char *beg, const char *end, const LangOptions &LO);

bool ParseScanfString(FormatStringHandler &H,
                      const char *beg, const char *end, const LangOptions &LO);

} // end analyze_format_string namespace
} // end clang namespace
#endif
