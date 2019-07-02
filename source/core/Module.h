/**
 *  @note This file is part of MABE, https://github.com/mercere99/MABE2
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2019
 *
 *  @file  Module.h
 *  @brief Base class for all MABE modules.
 */

#ifndef MABE_MODULE_H
#define MABE_MODULE_H

#include <string>

#include "base/map.h"
#include "base/Ptr.h"
#include "base/vector.h"
#include "tools/map_utils.h"
#include "tools/reference_vector.h"

namespace mabe {

  class MABE;
  class World;

  class Module {
  protected:
    std::string name;                 ///< Unique name for this module.
    emp::vector<std::string> errors;  ///< Has this class detected any configuration errors?

    // What type of module is this (note, some can be more than one!)
    bool is_evaluate=false;   ///< Does this module perform evaluation on organisms?
    bool is_select=false;     ///< Does this module select organisms to reproduce?
    bool is_placement=false;  ///< Does this module handle offspring placement?
    bool is_analyze=false;    ///< Does this module record or evaluate data?

    enum class ReplicationType {
      NO_PREFERENCE, REQUIRE_ASYNC, DEFAULT_ASYNC, DEFAULT_SYNC, REQUIRE_SYNC
    };
    ReplicationType rep_type = ReplicationType::NO_PREFERENCE;

    /// Which populations are we operating on?
    emp::reference_vector<mabe::Population> pops;  ///< Which population are we using?
    size_t required_pops = 0;                      ///< How many population are needed?

    /// Store information about organism traits that this module needs to work with.
    struct TraitInfo {
      std::string name="";
      std::string desc="";
      emp::TypeID type;

      /// Which modules are allowed to read or write this trait?
      enum Access {
        UNKNOWN,   ///< Access level unknown; most likely a problem!
        OWNED,     ///< Can READ & WRITE this trait; other modules can only read.
        SHARED,    ///< Can READ & WRITE this trait; other modules can too.
        REQUIRED,  ///< Can READ this trait, but another module must WRITE to it.
        PRIVATE    ///< Can READ & WRITE this trait.  Others cannot use it.
      };
      Access access = Access::UNKNOWN;

      /// How should this trait be initialized in a newly-born organism?
      /// * Injected organisms always use the default value.
      /// * Modules can moitor signals to make other changes at any time.
      enum Init {
        DEFAULT,   ///< Trait is initialized to a pre-set default value.
        PARENT,    ///< Trait is inhereted (from first parent if more than one)
        AVERAGE,   ///< Trait becomes average of all parents on birth.
        MINIMUM,   ///< Trait becomes lowest of all parents on birth.
        MAXIMUM    ///< Trait becomes highest of all parents on birth.
      };
      Init init = Init::DEFAULT;
      bool reset_parent = false;  ///< Should the parent ALSO be reset on birth?

      /// Which information should we store in the trait as we go?
      enum Archive {
        NONE,       ///< Don't store any older information.
        LAST_RESET, ///< Store value at reset in "last_(name)"
        ALL_RESET,  ///< Store values at all resets in "archive_(name)"
        ALL_CHANGE  ///< Store values from every change in "sequence_(name)"
        // @CAO: CHANGE not yet impements since hard to track...
      };
      Archive archive = Archive::NONE;

      virtual bool HasDefault() { return false; }

      /// Set the current value of this trait to be automatically inthereted by offspring.
      TraitInfo & SetInheritParent() { init = Init::PARENT; return *this; }

      /// Set the average across parents for this trait to be automatically inthereted by offspring.
      TraitInfo & SetInheritAverage() { init = Init::AVERAGE; return *this; }

      /// Set the minimum across parents for this trait to be automatically inthereted by offspring.
      TraitInfo & SetInheritMinimum() { init = Init::MINIMUM; return *this; }

      /// Set the maximum across parents for this trait to be automatically inthereted by offspring.
      TraitInfo & SetInheritMaximum() { init = Init::MAXIMUM; return *this; }

      /// Set the parent to ALSO reset to the same value as the offspring on divide.
      TraitInfo & SetParentReset() { reset_parent = true; return *this; }

      /// Set the previous value of this trait to be stored on birth or reset.
      TraitInfo & SetArchiveLast() { archive = Archive::LAST_RESET; return *this; }

      /// Set ALL previous values of this trait to be store after each reset.
      TraitInfo & SetArchiveAll() { archive = Archive::ALL_RESET; return *this; }
    };

    template <typename T>
    struct TypedTraitInfo : public TraitInfo {
      T default_value;
      bool has_default;

      TypedTraitInfo(const std::string & in_name) : has_default(false)
      {
        name = in_name;
        type = emp::GetTypeID<T>();
      }

      TypedTraitInfo(const std::string & in_name, const T & in_default)
        : default_value(in_default), has_default(true)
      {
        name = in_name;
        type = emp::GetTypeID<T>();
      }

      bool HasDefault() { return has_default; }

      TypedTraitInfo<T> & SetDefault(const T & in_default) {
        default_value = in_default;
        has_default = true;
        return *this;
      }
    };

    emp::map<std::string, emp::Ptr<TraitInfo>> trait_map;

    // Helper functions
    template <typename... Ts>
    void AddError(Ts &&... args) {
      errors.push_back( emp::to_string( std::forward<Ts>(args)... ));
    }

  public:
    Module() : name("") { ; }
    Module(const Module &) = default;
    Module(Module &&) = default;
    virtual ~Module() { ; }

    const std::string & GetName() const { return name; }
    const emp::vector<std::string> & GetErrors() const { return errors; }
    size_t GetRequiredPops() const { return required_pops; }

    virtual emp::Ptr<Module> Clone() { return nullptr; }

    bool IsEvaluate() const { return is_evaluate; }
    bool IsSelect() const { return is_select; }
    bool IsPlacement() const { return is_placement; }
    bool IsAnalyze() const { return is_analyze; }

    Module & IsEvaluate(bool in) noexcept { is_evaluate = in; return *this; }
    Module & IsSelect(bool in) noexcept { is_select = in; return *this; }
    Module & IsPlacement(bool in) noexcept { is_placement = in; return *this; }
    Module & IsAnalyze(bool in) noexcept { is_analyze = in; return *this; }

    Module & RequireAsync() { rep_type = ReplicationType::REQUIRE_ASYNC; return *this; }
    Module & DefaultAsync() { rep_type = ReplicationType::DEFAULT_ASYNC; return *this; }
    Module & DefaultSync() { rep_type = ReplicationType::DEFAULT_SYNC; return *this; }
    Module & RequireSync() { rep_type = ReplicationType::REQUIRE_SYNC; return *this; }

    // Internal, initial setup.
    void InternalSetup(mabe::World & world) {
      // World needs to check if all populations are setup correctly.
    }

    virtual void Setup(mabe::World &) { /* By default, assume no setup needed. */ }
    virtual void Update() { /* By default, do nothing at update. */ }

    /// Add an additional population to evaluate.
    Module & AddPopulation( mabe::Population & in_pop ) {
      pops.push_back( in_pop );
      return *this;
    }

  // --------------------- Functions to be used in derived modules ONLY --------------------------
  protected:

    void SetRequiredPops(size_t in_pops) { required_pops = in_pops; }

    // --== Trait management ==--
   
    /// Add a new trait to this module, specifying its access method, its name, and its description.
    template <typename T>
    TypedTraitInfo<T> & AddTrait(TraitInfo::Access access,
                                 const std::string & in_name,
                                 const std::string & desc) {
      if (emp::Has(trait_map, in_name)) {
        AddError("Module ", name, " is creating a duplicate trait named '", in_name, "'.");
      }
      auto new_ptr = emp::NewPtr<TypedTraitInfo<T>>(in_name, desc);
      new_ptr->access = access;
      trait_map[in_name] = new_ptr;
      return *new_ptr;
    }

    /// Add a new trait to this module, specifying its access method, its name, and its description
    /// AND its default value.
    template <typename T>
    TypedTraitInfo<T> & AddTrait(TraitInfo::Access access,
                                 const std::string & name,
                                 const std::string & desc,
                                 const T & default_val) {
      return AddTrait<T>(access, name, desc).SetDefault(default_val);
    }

    /// Add trait that this module can READ & WRITE this trait.  Others cannot use it.
    /// Must provide name, description, and a default value to start at.
    template <typename T>
    TraitInfo & AddPrivateTrait(const std::string & name, const std::string & desc, const T & default_val) {
      return AddTrait<T>(TraitInfo::Access::PRIVATE, name, desc, default_val);
    }

    /// Add trait that this module can READ & WRITE to; other modules can only read.
    /// Must provide name, description, and a default value to start at.
    template <typename T>
    TraitInfo & AddOwnedTrait(const std::string & name, const std::string & desc, const T & default_val) {
      return AddTrait<T>(TraitInfo::Access::OWNED, name, desc, default_val);
    }
   
    /// Add trait that this module can READ & WRITE this trait; other modules can too.
    /// Must provide name, description; a default value is optional, but at least one
    /// module MUST set and it must be consistent across all modules that use it.
    template <typename T>
    TraitInfo & AddSharedTrait(const std::string & name, const std::string & desc) {
      return AddTrait<T>(TraitInfo::Access::SHARED, name, desc);
    }
    template <typename T>
    TraitInfo & AddSharedTrait(const std::string & name, const std::string & desc, const T & default_val) {
      return AddTrait<T>(TraitInfo::Access::SHARED, name, desc, default_val);
    }
   
    /// Add trait that this module can READ this trait, but another module must WRITE to it.
    template <typename T>
    TraitInfo & AddRequiredTrait(const std::string & name, const std::string & desc) {
      return AddTrait<T>(TraitInfo::Access::REQUIRED, name, desc);
    }


  };

}

#endif
