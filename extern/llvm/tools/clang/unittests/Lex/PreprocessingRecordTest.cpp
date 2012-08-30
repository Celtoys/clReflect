//===- unittests/Lex/PreprocessingRecordTest.cpp - PreprocessingRecord tests =//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "llvm/Config/config.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace clang;

namespace {

// The test fixture.
class PreprocessingRecordTest : public ::testing::Test {
protected:
  PreprocessingRecordTest()
    : FileMgr(FileMgrOpts),
      DiagID(new DiagnosticIDs()),
      Diags(DiagID, new IgnoringDiagConsumer()),
      SourceMgr(Diags, FileMgr) {
    TargetOpts.Triple = "x86_64-apple-darwin11.1.0";
    Target = TargetInfo::CreateTargetInfo(Diags, TargetOpts);
  }

  FileSystemOptions FileMgrOpts;
  FileManager FileMgr;
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID;
  DiagnosticsEngine Diags;
  SourceManager SourceMgr;
  LangOptions LangOpts;
  TargetOptions TargetOpts;
  IntrusiveRefCntPtr<TargetInfo> Target;
};

class VoidModuleLoader : public ModuleLoader {
  virtual Module *loadModule(SourceLocation ImportLoc, ModuleIdPath Path,
                             Module::NameVisibilityKind Visibility,
                             bool IsInclusionDirective) {
    return 0;
  }
};

TEST_F(PreprocessingRecordTest, PPRecAPI) {
  const char *source =
      "0 1\n"
      "#if 1\n"
      "2\n"
      "#ifndef BB\n"
      "3 4\n"
      "#else\n"
      "#endif\n"
      "5\n"
      "#endif\n"
      "6\n"
      "#if 1\n"
      "7\n"
      "#if 1\n"
      "#endif\n"
      "8\n"
      "#endif\n"
      "9\n";

  MemoryBuffer *buf = MemoryBuffer::getMemBuffer(source);
  SourceMgr.createMainFileIDForMemBuffer(buf);

  VoidModuleLoader ModLoader;
  HeaderSearch HeaderInfo(FileMgr, Diags, LangOpts, Target.getPtr());
  Preprocessor PP(Diags, LangOpts,
                  Target.getPtr(),
                  SourceMgr, HeaderInfo, ModLoader,
                  /*IILookup =*/ 0,
                  /*OwnsHeaderSearch =*/false,
                  /*DelayInitialization =*/ false);
  PP.createPreprocessingRecord(true);
  PP.EnterMainSourceFile();

  std::vector<Token> toks;
  while (1) {
    Token tok;
    PP.Lex(tok);
    if (tok.is(tok::eof))
      break;
    toks.push_back(tok);
  }

  // Make sure we got the tokens that we expected.
  ASSERT_EQ(10U, toks.size());
  
  PreprocessingRecord &PPRec = *PP.getPreprocessingRecord();
  EXPECT_FALSE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[0].getLocation(), toks[1].getLocation())));
  EXPECT_TRUE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[0].getLocation(), toks[2].getLocation())));
  EXPECT_FALSE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[3].getLocation(), toks[4].getLocation())));
  EXPECT_TRUE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[1].getLocation(), toks[5].getLocation())));
  EXPECT_TRUE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[2].getLocation(), toks[6].getLocation())));
  EXPECT_FALSE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[2].getLocation(), toks[5].getLocation())));
  EXPECT_FALSE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[0].getLocation(), toks[6].getLocation())));
  EXPECT_TRUE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[2].getLocation(), toks[8].getLocation())));
  EXPECT_FALSE(PPRec.rangeIntersectsConditionalDirective(
                    SourceRange(toks[0].getLocation(), toks[9].getLocation())));

  EXPECT_TRUE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[0].getLocation(), toks[2].getLocation()));
  EXPECT_FALSE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[3].getLocation(), toks[4].getLocation()));
  EXPECT_TRUE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[1].getLocation(), toks[5].getLocation()));
  EXPECT_TRUE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[2].getLocation(), toks[0].getLocation()));
  EXPECT_FALSE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[4].getLocation(), toks[3].getLocation()));
  EXPECT_TRUE(PPRec.areInDifferentConditionalDirectiveRegion(
                    toks[5].getLocation(), toks[1].getLocation()));
}

} // anonymous namespace
