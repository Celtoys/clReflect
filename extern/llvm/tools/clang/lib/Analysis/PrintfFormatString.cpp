//== PrintfFormatString.cpp - Analysis of printf format strings --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Handling of format string in printf and friends.  The structure of format
// strings for fprintf() are described in C99 7.19.6.1.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/FormatString.h"
#include "FormatStringParsing.h"

using clang::analyze_format_string::ArgTypeResult;
using clang::analyze_format_string::FormatStringHandler;
using clang::analyze_format_string::LengthModifier;
using clang::analyze_format_string::OptionalAmount;
using clang::analyze_format_string::ConversionSpecifier;
using clang::analyze_printf::PrintfSpecifier;

using namespace clang;

typedef clang::analyze_format_string::SpecifierResult<PrintfSpecifier>
        PrintfSpecifierResult;

//===----------------------------------------------------------------------===//
// Methods for parsing format strings.
//===----------------------------------------------------------------------===//

using analyze_format_string::ParseNonPositionAmount;

static bool ParsePrecision(FormatStringHandler &H, PrintfSpecifier &FS,
                           const char *Start, const char *&Beg, const char *E,
                           unsigned *argIndex) {
  if (argIndex) {
    FS.setPrecision(ParseNonPositionAmount(Beg, E, *argIndex));
  } else {
    const OptionalAmount Amt = ParsePositionAmount(H, Start, Beg, E,
                                           analyze_format_string::PrecisionPos);
    if (Amt.isInvalid())
      return true;
    FS.setPrecision(Amt);
  }
  return false;
}

static PrintfSpecifierResult ParsePrintfSpecifier(FormatStringHandler &H,
                                                  const char *&Beg,
                                                  const char *E,
                                                  unsigned &argIndex,
                                                  const LangOptions &LO) {

  using namespace clang::analyze_format_string;
  using namespace clang::analyze_printf;

  const char *I = Beg;
  const char *Start = 0;
  UpdateOnReturn <const char*> UpdateBeg(Beg, I);

  // Look for a '%' character that indicates the start of a format specifier.
  for ( ; I != E ; ++I) {
    char c = *I;
    if (c == '\0') {
      // Detect spurious null characters, which are likely errors.
      H.HandleNullChar(I);
      return true;
    }
    if (c == '%') {
      Start = I++;  // Record the start of the format specifier.
      break;
    }
  }

  // No format specifier found?
  if (!Start)
    return false;

  if (I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  PrintfSpecifier FS;
  if (ParseArgPosition(H, FS, Start, I, E))
    return true;

  if (I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for flags (if any).
  bool hasMore = true;
  for ( ; I != E; ++I) {
    switch (*I) {
      default: hasMore = false; break;
      case '\'':
        // FIXME: POSIX specific.  Always accept?
        FS.setHasThousandsGrouping(I);
        break;
      case '-': FS.setIsLeftJustified(I); break;
      case '+': FS.setHasPlusPrefix(I); break;
      case ' ': FS.setHasSpacePrefix(I); break;
      case '#': FS.setHasAlternativeForm(I); break;
      case '0': FS.setHasLeadingZeros(I); break;
    }
    if (!hasMore)
      break;
  }

  if (I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for the field width (if any).
  if (ParseFieldWidth(H, FS, Start, I, E,
                      FS.usesPositionalArg() ? 0 : &argIndex))
    return true;

  if (I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for the precision (if any).
  if (*I == '.') {
    ++I;
    if (I == E) {
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }

    if (ParsePrecision(H, FS, Start, I, E,
                       FS.usesPositionalArg() ? 0 : &argIndex))
      return true;

    if (I == E) {
      // No more characters left?
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }
  }

  // Look for the length modifier.
  if (ParseLengthModifier(FS, I, E, LO) && I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  if (*I == '\0') {
    // Detect spurious null characters, which are likely errors.
    H.HandleNullChar(I);
    return true;
  }

  // Finally, look for the conversion specifier.
  const char *conversionPosition = I++;
  ConversionSpecifier::Kind k = ConversionSpecifier::InvalidSpecifier;
  switch (*conversionPosition) {
    default:
      break;
    // C99: 7.19.6.1 (section 8).
    case '%': k = ConversionSpecifier::PercentArg;   break;
    case 'A': k = ConversionSpecifier::AArg; break;
    case 'E': k = ConversionSpecifier::EArg; break;
    case 'F': k = ConversionSpecifier::FArg; break;
    case 'G': k = ConversionSpecifier::GArg; break;
    case 'X': k = ConversionSpecifier::XArg; break;
    case 'a': k = ConversionSpecifier::aArg; break;
    case 'c': k = ConversionSpecifier::cArg; break;
    case 'd': k = ConversionSpecifier::dArg; break;
    case 'e': k = ConversionSpecifier::eArg; break;
    case 'f': k = ConversionSpecifier::fArg; break;
    case 'g': k = ConversionSpecifier::gArg; break;
    case 'i': k = ConversionSpecifier::iArg; break;
    case 'n': k = ConversionSpecifier::nArg; break;
    case 'o': k = ConversionSpecifier::oArg; break;
    case 'p': k = ConversionSpecifier::pArg;   break;
    case 's': k = ConversionSpecifier::sArg;      break;
    case 'u': k = ConversionSpecifier::uArg; break;
    case 'x': k = ConversionSpecifier::xArg; break;
    // POSIX specific.
    case 'C': k = ConversionSpecifier::CArg; break;
    case 'S': k = ConversionSpecifier::SArg; break;
    // Objective-C.
    case '@': k = ConversionSpecifier::ObjCObjArg; break;
    // Glibc specific.
    case 'm': k = ConversionSpecifier::PrintErrno; break;
  }
  PrintfConversionSpecifier CS(conversionPosition, k);
  FS.setConversionSpecifier(CS);
  if (CS.consumesDataArgument() && !FS.usesPositionalArg())
    FS.setArgIndex(argIndex++);

  if (k == ConversionSpecifier::InvalidSpecifier) {
    // Assume the conversion takes one argument.
    return !H.HandleInvalidPrintfConversionSpecifier(FS, Start, I - Start);
  }
  return PrintfSpecifierResult(Start, FS);
}

bool clang::analyze_format_string::ParsePrintfString(FormatStringHandler &H,
                                                     const char *I,
                                                     const char *E,
                                                     const LangOptions &LO) {

  unsigned argIndex = 0;

  // Keep looking for a format specifier until we have exhausted the string.
  while (I != E) {
    const PrintfSpecifierResult &FSR = ParsePrintfSpecifier(H, I, E, argIndex,
                                                            LO);
    // Did a fail-stop error of any kind occur when parsing the specifier?
    // If so, don't do any more processing.
    if (FSR.shouldStop())
      return true;;
    // Did we exhaust the string or encounter an error that
    // we can recover from?
    if (!FSR.hasValue())
      continue;
    // We have a format specifier.  Pass it to the callback.
    if (!H.HandlePrintfSpecifier(FSR.getValue(), FSR.getStart(),
                                 I - FSR.getStart()))
      return true;
  }
  assert(I == E && "Format string not exhausted");
  return false;
}

//===----------------------------------------------------------------------===//
// Methods on PrintfSpecifier.
//===----------------------------------------------------------------------===//

ArgTypeResult PrintfSpecifier::getArgType(ASTContext &Ctx,
                                          bool IsObjCLiteral) const {
  const PrintfConversionSpecifier &CS = getConversionSpecifier();

  if (!CS.consumesDataArgument())
    return ArgTypeResult::Invalid();

  if (CS.getKind() == ConversionSpecifier::cArg)
    switch (LM.getKind()) {
      case LengthModifier::None: return Ctx.IntTy;
      case LengthModifier::AsLong:
        return ArgTypeResult(ArgTypeResult::WIntTy, "wint_t");
      default:
        return ArgTypeResult::Invalid();
    }

  if (CS.isIntArg())
    switch (LM.getKind()) {
      case LengthModifier::AsLongDouble:
        // GNU extension.
        return Ctx.LongLongTy;
      case LengthModifier::None: return Ctx.IntTy;
      case LengthModifier::AsChar: return ArgTypeResult::AnyCharTy;
      case LengthModifier::AsShort: return Ctx.ShortTy;
      case LengthModifier::AsLong: return Ctx.LongTy;
      case LengthModifier::AsLongLong:
      case LengthModifier::AsQuad:
        return Ctx.LongLongTy;
      case LengthModifier::AsIntMax:
        return ArgTypeResult(Ctx.getIntMaxType(), "intmax_t");
      case LengthModifier::AsSizeT:
        // FIXME: How to get the corresponding signed version of size_t?
        return ArgTypeResult();
      case LengthModifier::AsPtrDiff:
        return ArgTypeResult(Ctx.getPointerDiffType(), "ptrdiff_t");
      case LengthModifier::AsAllocate:
      case LengthModifier::AsMAllocate:
        return ArgTypeResult::Invalid();
    }

  if (CS.isUIntArg())
    switch (LM.getKind()) {
      case LengthModifier::AsLongDouble:
        // GNU extension.
        return Ctx.UnsignedLongLongTy;
      case LengthModifier::None: return Ctx.UnsignedIntTy;
      case LengthModifier::AsChar: return Ctx.UnsignedCharTy;
      case LengthModifier::AsShort: return Ctx.UnsignedShortTy;
      case LengthModifier::AsLong: return Ctx.UnsignedLongTy;
      case LengthModifier::AsLongLong:
      case LengthModifier::AsQuad:
        return Ctx.UnsignedLongLongTy;
      case LengthModifier::AsIntMax:
        return ArgTypeResult(Ctx.getUIntMaxType(), "uintmax_t");
      case LengthModifier::AsSizeT:
        return ArgTypeResult(Ctx.getSizeType(), "size_t");
      case LengthModifier::AsPtrDiff:
        // FIXME: How to get the corresponding unsigned
        // version of ptrdiff_t?
        return ArgTypeResult();
      case LengthModifier::AsAllocate:
      case LengthModifier::AsMAllocate:
        return ArgTypeResult::Invalid();
    }

  if (CS.isDoubleArg()) {
    if (LM.getKind() == LengthModifier::AsLongDouble)
      return Ctx.LongDoubleTy;
    return Ctx.DoubleTy;
  }

  switch (CS.getKind()) {
    case ConversionSpecifier::sArg:
      if (LM.getKind() == LengthModifier::AsWideChar) {
        if (IsObjCLiteral)
          return Ctx.getPointerType(Ctx.UnsignedShortTy.withConst());
        return ArgTypeResult(ArgTypeResult::WCStrTy, "wchar_t *");
      }
      return ArgTypeResult::CStrTy;
    case ConversionSpecifier::SArg:
      if (IsObjCLiteral)
        return Ctx.getPointerType(Ctx.UnsignedShortTy.withConst());
      return ArgTypeResult(ArgTypeResult::WCStrTy, "wchar_t *");
    case ConversionSpecifier::CArg:
      if (IsObjCLiteral)
        return Ctx.UnsignedShortTy;
      return ArgTypeResult(Ctx.WCharTy, "wchar_t");
    case ConversionSpecifier::pArg:
      return ArgTypeResult::CPointerTy;
    case ConversionSpecifier::ObjCObjArg:
      return ArgTypeResult::ObjCPointerTy;
    default:
      break;
  }

  // FIXME: Handle other cases.
  return ArgTypeResult();
}

bool PrintfSpecifier::fixType(QualType QT, const LangOptions &LangOpt,
                              ASTContext &Ctx, bool IsObjCLiteral) {
  // Handle strings first (char *, wchar_t *)
  if (QT->isPointerType() && (QT->getPointeeType()->isAnyCharacterType())) {
    CS.setKind(ConversionSpecifier::sArg);

    // Disable irrelevant flags
    HasAlternativeForm = 0;
    HasLeadingZeroes = 0;

    // Set the long length modifier for wide characters
    if (QT->getPointeeType()->isWideCharType())
      LM.setKind(LengthModifier::AsWideChar);
    else
      LM.setKind(LengthModifier::None);

    return true;
  }

  // We can only work with builtin types.
  const BuiltinType *BT = QT->getAs<BuiltinType>();
  if (!BT)
    return false;

  // Set length modifier
  switch (BT->getKind()) {
  case BuiltinType::Bool:
  case BuiltinType::WChar_U:
  case BuiltinType::WChar_S:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::UInt128:
  case BuiltinType::Int128:
  case BuiltinType::Half:
    // Various types which are non-trivial to correct.
    return false;

#define SIGNED_TYPE(Id, SingletonId)
#define UNSIGNED_TYPE(Id, SingletonId)
#define FLOATING_TYPE(Id, SingletonId)
#define BUILTIN_TYPE(Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
    // Misc other stuff which doesn't make sense here.
    return false;

  case BuiltinType::UInt:
  case BuiltinType::Int:
  case BuiltinType::Float:
  case BuiltinType::Double:
    LM.setKind(LengthModifier::None);
    break;

  case BuiltinType::Char_U:
  case BuiltinType::UChar:
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    LM.setKind(LengthModifier::AsChar);
    break;

  case BuiltinType::Short:
  case BuiltinType::UShort:
    LM.setKind(LengthModifier::AsShort);
    break;

  case BuiltinType::Long:
  case BuiltinType::ULong:
    LM.setKind(LengthModifier::AsLong);
    break;

  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    LM.setKind(LengthModifier::AsLongLong);
    break;

  case BuiltinType::LongDouble:
    LM.setKind(LengthModifier::AsLongDouble);
    break;
  }

  // Handle size_t, ptrdiff_t, etc. that have dedicated length modifiers in C99.
  if (isa<TypedefType>(QT) && (LangOpt.C99 || LangOpt.CPlusPlus0x)) {
    const IdentifierInfo *Identifier = QT.getBaseTypeIdentifier();
    if (Identifier->getName() == "size_t") {
      LM.setKind(LengthModifier::AsSizeT);
    } else if (Identifier->getName() == "ssize_t") {
      // Not C99, but common in Unix.
      LM.setKind(LengthModifier::AsSizeT);
    } else if (Identifier->getName() == "intmax_t") {
      LM.setKind(LengthModifier::AsIntMax);
    } else if (Identifier->getName() == "uintmax_t") {
      LM.setKind(LengthModifier::AsIntMax);
    } else if (Identifier->getName() == "ptrdiff_t") {
      LM.setKind(LengthModifier::AsPtrDiff);
    }
  }

  // If fixing the length modifier was enough, we are done.
  const analyze_printf::ArgTypeResult &ATR = getArgType(Ctx, IsObjCLiteral);
  if (hasValidLengthModifier() && ATR.isValid() && ATR.matchesType(Ctx, QT))
    return true;

  // Set conversion specifier and disable any flags which do not apply to it.
  // Let typedefs to char fall through to int, as %c is silly for uint8_t.
  if (isa<TypedefType>(QT) && QT->isAnyCharacterType()) {
    CS.setKind(ConversionSpecifier::cArg);
    LM.setKind(LengthModifier::None);
    Precision.setHowSpecified(OptionalAmount::NotSpecified);
    HasAlternativeForm = 0;
    HasLeadingZeroes = 0;
    HasPlusPrefix = 0;
  }
  // Test for Floating type first as LongDouble can pass isUnsignedIntegerType
  else if (QT->isRealFloatingType()) {
    CS.setKind(ConversionSpecifier::fArg);
  }
  else if (QT->isSignedIntegerType()) {
    CS.setKind(ConversionSpecifier::dArg);
    HasAlternativeForm = 0;
  }
  else if (QT->isUnsignedIntegerType()) {
    CS.setKind(ConversionSpecifier::uArg);
    HasAlternativeForm = 0;
    HasPlusPrefix = 0;
  } else {
    llvm_unreachable("Unexpected type");
  }

  return true;
}

void PrintfSpecifier::toString(raw_ostream &os) const {
  // Whilst some features have no defined order, we are using the order
  // appearing in the C99 standard (ISO/IEC 9899:1999 (E) 7.19.6.1)
  os << "%";

  // Positional args
  if (usesPositionalArg()) {
    os << getPositionalArgIndex() << "$";
  }

  // Conversion flags
  if (IsLeftJustified)    os << "-";
  if (HasPlusPrefix)      os << "+";
  if (HasSpacePrefix)     os << " ";
  if (HasAlternativeForm) os << "#";
  if (HasLeadingZeroes)   os << "0";

  // Minimum field width
  FieldWidth.toString(os);
  // Precision
  Precision.toString(os);
  // Length modifier
  os << LM.toString();
  // Conversion specifier
  os << CS.toString();
}

bool PrintfSpecifier::hasValidPlusPrefix() const {
  if (!HasPlusPrefix)
    return true;

  // The plus prefix only makes sense for signed conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidAlternativeForm() const {
  if (!HasAlternativeForm)
    return true;

  // Alternate form flag only valid with the oxXaAeEfFgG conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidLeadingZeros() const {
  if (!HasLeadingZeroes)
    return true;

  // Leading zeroes flag only valid with the diouxXaAeEfFgG conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::uArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidSpacePrefix() const {
  if (!HasSpacePrefix)
    return true;

  // The space prefix only makes sense for signed conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
    return true;

  default:
    return false;
  }
}

bool PrintfSpecifier::hasValidLeftJustified() const {
  if (!IsLeftJustified)
    return true;

  // The left justified flag is valid for all conversions except n
  switch (CS.getKind()) {
  case ConversionSpecifier::nArg:
    return false;

  default:
    return true;
  }
}

bool PrintfSpecifier::hasValidThousandsGroupingPrefix() const {
  if (!HasThousandsGrouping)
    return true;

  switch (CS.getKind()) {
    case ConversionSpecifier::dArg:
    case ConversionSpecifier::iArg:
    case ConversionSpecifier::uArg:
    case ConversionSpecifier::fArg:
    case ConversionSpecifier::FArg:
    case ConversionSpecifier::gArg:
    case ConversionSpecifier::GArg:
      return true;
    default:
      return false;
  }
}

bool PrintfSpecifier::hasValidPrecision() const {
  if (Precision.getHowSpecified() == OptionalAmount::NotSpecified)
    return true;

  // Precision is only valid with the diouxXaAeEfFgGs conversions
  switch (CS.getKind()) {
  case ConversionSpecifier::dArg:
  case ConversionSpecifier::iArg:
  case ConversionSpecifier::oArg:
  case ConversionSpecifier::uArg:
  case ConversionSpecifier::xArg:
  case ConversionSpecifier::XArg:
  case ConversionSpecifier::aArg:
  case ConversionSpecifier::AArg:
  case ConversionSpecifier::eArg:
  case ConversionSpecifier::EArg:
  case ConversionSpecifier::fArg:
  case ConversionSpecifier::FArg:
  case ConversionSpecifier::gArg:
  case ConversionSpecifier::GArg:
  case ConversionSpecifier::sArg:
    return true;

  default:
    return false;
  }
}
bool PrintfSpecifier::hasValidFieldWidth() const {
  if (FieldWidth.getHowSpecified() == OptionalAmount::NotSpecified)
      return true;

  // The field width is valid for all conversions except n
  switch (CS.getKind()) {
  case ConversionSpecifier::nArg:
    return false;

  default:
    return true;
  }
}
