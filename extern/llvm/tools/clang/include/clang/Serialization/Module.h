//===--- Module.h - Module description --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Module class, which describes a module that has
//  been loaded from an AST file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_MODULE_H
#define LLVM_CLANG_SERIALIZATION_MODULE_H

#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include <string>

namespace clang {

class DeclContext;
class Module;
template<typename Info> class OnDiskChainedHashTable;

namespace serialization {

namespace reader {
  class ASTDeclContextNameLookupTrait;
}

/// \brief Specifies the kind of module that has been loaded.
enum ModuleKind {
  MK_Module,   ///< File is a module proper.
  MK_PCH,      ///< File is a PCH file treated as such.
  MK_Preamble, ///< File is a PCH file treated as the preamble.
  MK_MainFile  ///< File is a PCH file treated as the actual main file.
};

/// \brief Information about the contents of a DeclContext.
struct DeclContextInfo {
  DeclContextInfo()
    : NameLookupTableData(), LexicalDecls(), NumLexicalDecls() {}

  OnDiskChainedHashTable<reader::ASTDeclContextNameLookupTrait>
    *NameLookupTableData; // an ASTDeclContextNameLookupTable.
  const KindDeclIDPair *LexicalDecls;
  unsigned NumLexicalDecls;
};

/// \brief Information about a module that has been loaded by the ASTReader.
///
/// Each instance of the Module class corresponds to a single AST file, which
/// may be a precompiled header, precompiled preamble, a module, or an AST file
/// of some sort loaded as the main file, all of which are specific formulations
/// of the general notion of a "module". A module may depend on any number of
/// other modules.
class ModuleFile {
public:
  ModuleFile(ModuleKind Kind, unsigned Generation);
  ~ModuleFile();

  // === General information ===

  /// \brief The type of this module.
  ModuleKind Kind;

  /// \brief The file name of the module file.
  std::string FileName;

  /// \brief Whether this module has been directly imported by the
  /// user.
  bool DirectlyImported;

  /// \brief The generation of which this module file is a part.
  unsigned Generation;
  
  /// \brief The memory buffer that stores the data associated with
  /// this AST file.
  OwningPtr<llvm::MemoryBuffer> Buffer;

  /// \brief The size of this file, in bits.
  uint64_t SizeInBits;

  /// \brief The global bit offset (or base) of this module
  uint64_t GlobalBitOffset;

  /// \brief The bitstream reader from which we'll read the AST file.
  llvm::BitstreamReader StreamFile;

  /// \brief The main bitstream cursor for the main block.
  llvm::BitstreamCursor Stream;

  /// \brief The source location where this module was first imported.
  SourceLocation ImportLoc;

  /// \brief The first source location in this module.
  SourceLocation FirstLoc;

  // === Source Locations ===

  /// \brief Cursor used to read source location entries.
  llvm::BitstreamCursor SLocEntryCursor;

  /// \brief The number of source location entries in this AST file.
  unsigned LocalNumSLocEntries;

  /// \brief The base ID in the source manager's view of this module.
  int SLocEntryBaseID;

  /// \brief The base offset in the source manager's view of this module.
  unsigned SLocEntryBaseOffset;

  /// \brief Offsets for all of the source location entries in the
  /// AST file.
  const uint32_t *SLocEntryOffsets;

  /// \brief SLocEntries that we're going to preload.
  SmallVector<uint64_t, 4> PreloadSLocEntries;

  /// \brief The number of source location file entries in this AST file.
  unsigned LocalNumSLocFileEntries;

  /// \brief Offsets for all of the source location file entries in the
  /// AST file.
  const uint32_t *SLocFileOffsets;

  /// \brief Remapping table for source locations in this module.
  ContinuousRangeMap<uint32_t, int, 2> SLocRemap;

  // === Identifiers ===

  /// \brief The number of identifiers in this AST file.
  unsigned LocalNumIdentifiers;

  /// \brief Offsets into the identifier table data.
  ///
  /// This array is indexed by the identifier ID (-1), and provides
  /// the offset into IdentifierTableData where the string data is
  /// stored.
  const uint32_t *IdentifierOffsets;

  /// \brief Base identifier ID for identifiers local to this module.
  serialization::IdentID BaseIdentifierID;

  /// \brief Remapping table for identifier IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> IdentifierRemap;

  /// \brief Actual data for the on-disk hash table of identifiers.
  ///
  /// This pointer points into a memory buffer, where the on-disk hash
  /// table for identifiers actually lives.
  const char *IdentifierTableData;

  /// \brief A pointer to an on-disk hash table of opaque type
  /// IdentifierHashTable.
  void *IdentifierLookupTable;

  // === Macros ===

  /// \brief The cursor to the start of the preprocessor block, which stores
  /// all of the macro definitions.
  llvm::BitstreamCursor MacroCursor;

  /// \brief The offset of the start of the set of defined macros.
  uint64_t MacroStartOffset;

  // === Detailed PreprocessingRecord ===

  /// \brief The cursor to the start of the (optional) detailed preprocessing
  /// record block.
  llvm::BitstreamCursor PreprocessorDetailCursor;

  /// \brief The offset of the start of the preprocessor detail cursor.
  uint64_t PreprocessorDetailStartOffset;

  /// \brief Base preprocessed entity ID for preprocessed entities local to
  /// this module.
  serialization::PreprocessedEntityID BasePreprocessedEntityID;

  /// \brief Remapping table for preprocessed entity IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> PreprocessedEntityRemap;

  const PPEntityOffset *PreprocessedEntityOffsets;
  unsigned NumPreprocessedEntities;

  // === Header search information ===

  /// \brief The number of local HeaderFileInfo structures.
  unsigned LocalNumHeaderFileInfos;

  /// \brief Actual data for the on-disk hash table of header file
  /// information.
  ///
  /// This pointer points into a memory buffer, where the on-disk hash
  /// table for header file information actually lives.
  const char *HeaderFileInfoTableData;

  /// \brief The on-disk hash table that contains information about each of
  /// the header files.
  void *HeaderFileInfoTable;

  /// \brief Actual data for the list of framework names used in the header
  /// search information.
  const char *HeaderFileFrameworkStrings;

  // === Submodule information ===  
  /// \brief The number of submodules in this module.
  unsigned LocalNumSubmodules;
  
  /// \brief Base submodule ID for submodules local to this module.
  serialization::SubmoduleID BaseSubmoduleID;
  
  /// \brief Remapping table for submodule IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> SubmoduleRemap;
  
  // === Selectors ===

  /// \brief The number of selectors new to this file.
  ///
  /// This is the number of entries in SelectorOffsets.
  unsigned LocalNumSelectors;

  /// \brief Offsets into the selector lookup table's data array
  /// where each selector resides.
  const uint32_t *SelectorOffsets;

  /// \brief Base selector ID for selectors local to this module.
  serialization::SelectorID BaseSelectorID;

  /// \brief Remapping table for selector IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> SelectorRemap;

  /// \brief A pointer to the character data that comprises the selector table
  ///
  /// The SelectorOffsets table refers into this memory.
  const unsigned char *SelectorLookupTableData;

  /// \brief A pointer to an on-disk hash table of opaque type
  /// ASTSelectorLookupTable.
  ///
  /// This hash table provides the IDs of all selectors, and the associated
  /// instance and factory methods.
  void *SelectorLookupTable;

  // === Declarations ===

  /// DeclsCursor - This is a cursor to the start of the DECLS_BLOCK block. It
  /// has read all the abbreviations at the start of the block and is ready to
  /// jump around with these in context.
  llvm::BitstreamCursor DeclsCursor;

  /// \brief The number of declarations in this AST file.
  unsigned LocalNumDecls;

  /// \brief Offset of each declaration within the bitstream, indexed
  /// by the declaration ID (-1).
  const DeclOffset *DeclOffsets;

  /// \brief Base declaration ID for declarations local to this module.
  serialization::DeclID BaseDeclID;

  /// \brief Remapping table for declaration IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> DeclRemap;

  /// \brief Mapping from the module files that this module file depends on
  /// to the base declaration ID for that module as it is understood within this
  /// module.
  ///
  /// This is effectively a reverse global-to-local mapping for declaration
  /// IDs, so that we can interpret a true global ID (for this translation unit)
  /// as a local ID (for this module file).
  llvm::DenseMap<ModuleFile *, serialization::DeclID> GlobalToLocalDeclIDs;

  /// \brief The number of C++ base specifier sets in this AST file.
  unsigned LocalNumCXXBaseSpecifiers;

  /// \brief Offset of each C++ base specifier set within the bitstream,
  /// indexed by the C++ base specifier set ID (-1).
  const uint32_t *CXXBaseSpecifiersOffsets;

  typedef llvm::DenseMap<const DeclContext *, DeclContextInfo>
  DeclContextInfosMap;

  /// \brief Information about the lexical and visible declarations
  /// for each DeclContext.
  DeclContextInfosMap DeclContextInfos;

  /// \brief Array of file-level DeclIDs sorted by file.
  const serialization::DeclID *FileSortedDecls;

  /// \brief Array of redeclaration chain location information within this 
  /// module file, sorted by the first declaration ID.
  const serialization::LocalRedeclarationsInfo *RedeclarationsMap;

  /// \brief The number of redeclaration info entries in RedeclarationsMap.
  unsigned LocalNumRedeclarationsInMap;
  
  /// \brief The redeclaration chains for declarations local to this
  /// module file.
  SmallVector<uint64_t, 1> RedeclarationChains;
  
  /// \brief Array of category list location information within this 
  /// module file, sorted by the definition ID.
  const serialization::ObjCCategoriesInfo *ObjCCategoriesMap;
  
  /// \brief The number of redeclaration info entries in ObjCCategoriesMap.
  unsigned LocalNumObjCCategoriesInMap;
  
  /// \brief The Objective-C category lists for categories known to this
  /// module.
  SmallVector<uint64_t, 1> ObjCCategories;

  // === Types ===

  /// \brief The number of types in this AST file.
  unsigned LocalNumTypes;

  /// \brief Offset of each type within the bitstream, indexed by the
  /// type ID, or the representation of a Type*.
  const uint32_t *TypeOffsets;

  /// \brief Base type ID for types local to this module as represented in
  /// the global type ID space.
  serialization::TypeID BaseTypeIndex;

  /// \brief Remapping table for type IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> TypeRemap;

  // === Miscellaneous ===

  /// \brief Diagnostic IDs and their mappings that the user changed.
  SmallVector<uint64_t, 8> PragmaDiagMappings;

  /// \brief The AST stat cache installed for this file, if any.
  ///
  /// The dynamic type of this stat cache is always ASTStatCache
  void *StatCache;

  /// \brief List of modules which depend on this module
  llvm::SetVector<ModuleFile *> ImportedBy;

  /// \brief List of modules which this module depends on
  llvm::SetVector<ModuleFile *> Imports;

  /// \brief Determine whether this module was directly imported at
  /// any point during translation.
  bool isDirectlyImported() const { return DirectlyImported; }

  /// \brief Dump debugging output for this module.
  void dump();
};

} // end namespace serialization

} // end namespace clang

#endif
