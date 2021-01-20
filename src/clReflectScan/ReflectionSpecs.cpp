
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "ReflectionSpecs.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/Logging.h>

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclGroup.h>

namespace
{
    clang::AnnotateAttr* GetReflectionSpec(clang::DeclContext::decl_iterator i)
    {
        // Ignore anything that's not a namespace
        clang::NamespaceDecl* ns_decl = llvm::dyn_cast<clang::NamespaceDecl>(*i);
        if (ns_decl == 0)
            return 0;

        // Looking for internal registration namespaces
        if (ns_decl->getName() != "clcpp_internal")
            return 0;

        // Immediately prevent this namespace from being parsed by subsequent passes
        // NOTE: I tried calling removeDecl from within the translation unit but it was asserting
        // in some context-singleton code which I couldn't fully grep. This is an alternative.
        ns_decl->setInvalidDecl();

        // Get the first declaration
        clang::DeclContext::decl_iterator j = ns_decl->decls_begin();
        if (j == ns_decl->decls_end())
        {
            LOG(spec, WARNING, "Ill-formed Reflection Spec; no body found\n");
            return 0;
        }

        // Cast to a C++ record
        clang::CXXRecordDecl* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(*j);
        if (record_decl == 0)
        {
            LOG(spec, WARNING, "Ill-formed Reflection Spec; first declaration must be a reflection structure\n");
            return 0;
        }

        // Get the first attribute for the C++ record
        clang::specific_attr_iterator<clang::AnnotateAttr> k = record_decl->specific_attr_begin<clang::AnnotateAttr>();
        if (k == record_decl->specific_attr_end<clang::AnnotateAttr>())
        {
            LOG(spec, WARNING, "Ill-formed Reflection Spec; no annotation attribute found on the reflection structure\n");
            return 0;
        }

        return *k;
    }
}

ReflectionSpecs::ReflectionSpecs(const std::string& spec_log)
{
    LOG_TO_STDOUT(spec, WARNING);
    LOG_TO_STDOUT(spec, ERROR);

    if (spec_log != "")
        LOG_TO_FILE(spec, ALL, spec_log.c_str());
}

void ReflectionSpecs::Gather(clang::TranslationUnitDecl* tu_decl)
{
    // Iterate over every reflection spec in the translation unit
    clang::DeclContext::decl_iterator i = tu_decl->decls_begin();
    while (i != tu_decl->decls_end())
    {
        clang::AnnotateAttr* attribute = GetReflectionSpec(i);
        if (attribute == 0)
        {
            ++i;
            continue;
        }

        // Decode the reflection spec type
        ReflectionSpecType type = RST_None;
        llvm::StringRef reflect_spec = attribute->getAnnotation();
        if (reflect_spec.startswith("full-"))
            type = RST_Full;
        else if (reflect_spec.startswith("part-"))
            type = RST_Partial;
        else if (reflect_spec.startswith("container-"))
            type = RST_Container;
        else
            LOG(spec, WARNING, "Ill-formed Reflection Spec; couldn't figure out what type it is\n");

        if (type == RST_Full || type == RST_Partial)
        {
            AddReflectionSpec(reflect_spec.substr(5).str(), type);
        }

        else if (type == RST_Container)
        {
            // Split the fields of the annotation
            llvm::SmallVector<llvm::StringRef, 5> info;
            reflect_spec.split(info, "-");
            if (info.size() != 5)
            {
                LOG(spec, WARNING, "Ill-formed Reflection Spec Container; element count doesn't match expected count\n");
                ++i;
                continue;
            }

            // Parse the type info
            ReflectionSpecContainer rsc;
            rsc.read_iterator_type = info[2].str();
            rsc.write_iterator_type = info[3].str();
            rsc.has_key = false;

            // Parse the key info
            if (info[4] == "haskey")
                rsc.has_key = true;
            else if (info[4] != "nokey")
            {
                LOG(spec, WARNING, "Ill-formed Reflection Spec Container; expecting 'haskey' or 'nokey' as last parameter\n");
                ++i;
                continue;
            }

            m_ContainerSpecs[info[1].str()] = rsc;
            LOG(spec, INFO, "Reflection Spec Container: %s / %s / %s / %s\n", info[1].str().c_str(), info[2].str().c_str(),
                info[3].str().c_str(), info[4].str().c_str());
        }

        ++i;
    }
}

ReflectionSpecType ReflectionSpecs::Get(const std::string& name) const
{
    // Search for a reflection spec attached to this symbol
    ReflectionSpecMap::const_iterator i = m_ReflectionSpecs.find(name);
    if (i == m_ReflectionSpecs.end())
        return RST_None;

    return i->second;
}

void ReflectionSpecs::AddReflectionSpec(const std::string& symbol, ReflectionSpecType type)
{
    // Check for existence in the map before adding
    if (m_ReflectionSpecs.find(symbol) == m_ReflectionSpecs.end())
    {
        m_ReflectionSpecs[symbol] = type;
        LOG(spec, INFO, "Reflection Spec: %s (%s)\n", symbol.c_str(), type == RST_Full ? "full" : "partial");
    }
}
