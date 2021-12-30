/**
 *  @note This file is part of MABE, https://github.com/mercere99/MABE2
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2021.
 *
 *  @file  MABEScript.hpp
 *  @brief Customized Emplode scripting language instance for MABE runs.
 */

#ifndef MABE_MABE_SCRIPT_HPP
#define MABE_MABE_SCRIPT_HPP

#include <limits>
#include <string>
#include <sstream>

#include "emp/base/array.hpp"
#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/data/DataMap.hpp"
#include "emp/data/DataMapParser.hpp"
#include "emp/datastructs/vector_utils.hpp"
#include "emp/math/Random.hpp"
#include "emp/tools/string_utils.hpp"

#include "../Emplode/Emplode.hpp"

#include "Collection.hpp"
#include "data_collect.hpp"
#include "MABEBase.hpp"
#include "ModuleBase.hpp"
#include "Population.hpp"
#include "SigListener.hpp"
#include "TraitManager.hpp"

namespace mabe {

  ///  @brief The MABE scripting language.

  class MABEScript : public emplode::Emplode {
  private:
    MABEBase & control;
    emp::DataMapParser dm_parser;       ///< Parser to process functions on a data map

  public:
    /// Build a function to scan a data map, run a provided equation on its entries,
    /// and return the result.
    auto BuildTraitEquation(const emp::DataLayout & data_layout, std::string equation) {
      equation = Preprocess(equation);
      auto dm_fun = dm_parser.BuildMathFunction(data_layout, equation);
      return [dm_fun](const Organism & org){ return dm_fun(org.GetDataMap()); };
    }

    /// Scan an equation and return the names of all traits it is using.
    const std::set<std::string> & GetEquationTraits(const std::string & equation) {
      return dm_parser.GetNamesUsed(equation);
    }

    /// Find any instances of ${X} and eval the X.
    std::string Preprocess(const std::string & in_string) {
      std::string out_string = in_string;

      // Seek out instances of "${" to indicate the start of pre-processing.
      for (size_t i = 0; i < out_string.size(); ++i) {
        if (out_string[i] != '$') continue;   // Replacement tag must start with a '$'.
        if (out_string.size() <= i+2) break;  // Not enough room for a replacement tag.
        if (out_string[i+1] == '$') {         // Compress two $$ into one $
          out_string.erase(i,1);
          continue;
        }
        if (out_string[i+1] != '{') continue; // Eval must be surrounded by braces.

        // If we made it this far, we have a starting match!
        size_t end_pos = emp::find_paren_match(out_string, i+1, '{', '}', false);
        if (end_pos == i+1) return out_string;  // No end brace found!  @CAO -- exception here?
        const std::string new_text = Execute(emp::view_string_range(out_string, i+2, end_pos));
        out_string.replace(i, end_pos-i+1, new_text);

        i += new_text.size(); // Continue from the end point...
      }

      return out_string;
    }


    /// Build a function to scan a collection of organisms, calculating a given trait_fun for each,
    /// aggregating those values based on the mode, and returning the result as the specifed type.
    ///
    ///  'mode' option are:
    ///   <none>      : Default to the value of the trait for the first organism in the collection.
    ///   [ID]        : Value of this trait for the organism at the given index of the collection.
    ///   [OP][VALUE] : Count how often this value has the [OP] relationship with [VALUE].
    ///                  [OP] can be ==, !=, <, >, <=, or >=
    ///                  [VALUE] can be any numeric value
    ///   [OP][TRAIT] : Count how often this trait has the [OP] relationship with [TRAIT]
    ///                  [OP] can be ==, !=, <, >, <=, or >=
    ///                  [TRAIT] can be any other trait name
    ///   unique      : Return the number of distinct value for this trait (alias="richness").
    ///   mode        : Return the most common value in this collection (aliases="dom","dominant").
    ///   min         : Return the smallest value of this trait present.
    ///   max         : Return the largest value of this trait present.
    ///   ave         : Return the average value of this trait (alias="mean").
    ///   median      : Return the median value of this trait.
    ///   variance    : Return the variance of this trait.
    ///   stddev      : Return the standard deviation of this trait.
    ///   sum         : Return the summation of all values of this trait (alias="total")
    ///   entropy     : Return the Shannon entropy of this value.
    ///   :trait      : Return the mutual information with another provided trait.

    template <typename FROM_T=Collection, typename TO_T=std::string>
    std::function<TO_T(const FROM_T &)> BuildTraitSummary(
      std::string trait_fun,         // Function to calculate on each organism
      std::string mode,              // Method to combine organism results
      emp::DataLayout & data_layout  // DataLayout to assume for this summary
    ) {
      static_assert( std::is_same<FROM_T,Collection>() ||  std::is_same<FROM_T,Population>(),
                    "BuildTraitSummary FROM_T must be Collection or Population." );
      static_assert( std::is_same<TO_T,double>() ||  std::is_same<TO_T,std::string>(),
                    "BuildTraitSummary TO_T must be double or std::string." );

      // Pre-process the trait function to allow for use of regular config variables.
      trait_fun = Preprocess(trait_fun);

      // The trait input has two components:
      // (1) the trait (or trait function) and
      // (2) how to calculate the trait SUMMARY, such as min, max, ave, etc.

      // If we have a single trait, we may want to use a string type.
      if (emp::is_identifier(trait_fun)           // If we have a single trait...
          && data_layout.HasName(trait_fun)      // ...and it's in the data map...
          && !data_layout.IsNumeric(trait_fun)   // ...and it's not numeric...
      ) {
        size_t trait_id = data_layout.GetID(trait_fun);
        emp::TypeID result_type = data_layout.GetType(trait_id);

        auto get_fun = [trait_id, result_type](const Organism & org) {
          return emp::to_literal( org.GetTraitAsString(trait_id, result_type) );
        };
        auto fun = emp::BuildCollectFun<std::string, Collection>(mode, get_fun);

        // Go through all combinations of TO/FROM to return the correct types.
        if constexpr (std::is_same<FROM_T,Population>() && std::is_same<TO_T,double>()) {
          return [fun](const FROM_T & p){ return emp::from_string<double>(fun( Collection(p) )); };
        }
        else if constexpr (std::is_same<FROM_T,Collection>() && std::is_same<TO_T,double>()) {
          return [fun](const FROM_T & c){ return emp::from_string<double>(fun(c)); };
        }
        else if constexpr (std::is_same<FROM_T,Population>() && std::is_same<TO_T,std::string>()) {
          return [fun](const FROM_T & p){ return fun( Collection(p) ); };
        }
        else return fun;
      }

      // If we made it here, we are numeric.
      auto get_fun = BuildTraitEquation(data_layout, trait_fun);
      auto fun = emp::BuildCollectFun<double, Collection>(mode, get_fun);

      // If we don't have a fun, we weren't able to build an aggregation function.
      if (!fun) {
        emp::notify::Error("Unknown trait filter '", mode, "' for trait '", trait_fun, "'.");
        return [](const FROM_T &){ return TO_T(); };
      }

      // Go through all combinations of TO/FROM to return the correct types.
      // @CAO need to adjust BuildCollectFun so that it returns correct type; not always string.
      if constexpr (std::is_same<FROM_T,Population>() && std::is_same<TO_T,double>()) {
        return [fun](const Population & p){ return emp::from_string<double>(fun( Collection(p) )); };
      }
      else if constexpr (std::is_same<FROM_T,Collection>() && std::is_same<TO_T,double>()) {
        return [fun](const Collection & c){ return emp::from_string<double>(fun(c)); };
      }
      else if constexpr (std::is_same<FROM_T,Population>() && std::is_same<TO_T,std::string>()) {
        return [fun](const Population & p){ return fun( Collection(p) ); };
      }
      else return fun;
    }


    /// Build a function that takes a trait equation, builds it, and runs it on a container.
    /// Output is a function in the form:  TO_T(const FROM_T &, string equation, TO_T default)
    template <typename FROM_T=Collection, typename TO_T=std::string> 
    auto BuildTraitFunction(const std::string & fun_type, TO_T default_val=TO_T{}) {
      return [this,fun_type,default_val](FROM_T & pop, const std::string & equation) {
        if (pop.IsEmpty()) return default_val;
        auto trait_fun = BuildTraitSummary<FROM_T,TO_T>(equation, fun_type, pop.GetDataLayout());
        return trait_fun(pop);
      };
    }

  private:
    /// ======= Helper functions ===

    /// Set up all of the functions and globals in MABEScript
    void Initialize() {
      // Setup main MABE variables.
      auto & root_scope = GetSymbolTable().GetRootScope();
      root_scope.LinkFuns<int>("random_seed",
                              [this](){ return control.GetRandomSeed(); },
                              [this](int seed){ control.SetRandomSeed(seed); },
                              "Seed for random number generator; use 0 to base on time.");

      // Setup "Population" as a type in the config file.
      auto pop_init_fun = [this](const std::string & name) { return &control.AddPopulation(name); };
      auto pop_copy_fun = [this](const EmplodeType & from, EmplodeType & to) {
        emp::Ptr<const Population> from_pop = dynamic_cast<const Population *>(&from);
        emp::Ptr<Population> to_pop = dynamic_cast<Population *>(&to);
        if (!from_pop || !to_pop) return false; // Wrong type!
        control.CopyPop(*from_pop, *to_pop);    // Do the actual copy.
        return true;
      };
      auto & pop_type = AddType<Population>("Population", "Collection of organisms",
                                             pop_init_fun, pop_copy_fun);

      // Setup "Collection" as another config type.
      auto & collect_type = AddType<Collection>("OrgList", "Collection of organism pointers");

      pop_type.AddMemberFunction("REPLACE_WITH",
        [this](Population & to_pop, Population & from_pop){
          control.MoveOrgs(from_pop, to_pop, true); return 0;
        }, "Move all organisms organisms from another population, removing current orgs." );
      pop_type.AddMemberFunction("APPEND",
        [this](Population & to_pop, Population & from_pop){
          control.MoveOrgs(from_pop, to_pop, false); return 0;
        }, "Move all organisms organisms from another population, adding after current orgs." );

      pop_type.AddMemberFunction("TRAIT", BuildTraitFunction<Population,std::string>("0"),
        "Return the value of the provided trait for the first organism");
      pop_type.AddMemberFunction("CALC_RICHNESS", BuildTraitFunction<Population,double>("richness"),
        "Count the number of distinct values of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_MODE", BuildTraitFunction<Population,std::string>("mode"),
        "Identify the most common value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_MEAN", BuildTraitFunction<Population,double>("mean"),
        "Calculate the average value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_MIN", BuildTraitFunction<Population,double>("min"),
        "Find the smallest value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_MAX", BuildTraitFunction<Population,double>("max"),
        "Find the largest value of a trait (or equation).");
      pop_type.AddMemberFunction("ID_MIN", BuildTraitFunction<Population,double>("min_id"),
        "Find the index of the smallest value of a trait (or equation).");
      pop_type.AddMemberFunction("ID_MAX", BuildTraitFunction<Population,double>("max_id"),
        "Find the index of the largest value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_MEDIAN", BuildTraitFunction<Population,double>("median"),
        "Find the 50-percentile value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_VARIANCE", BuildTraitFunction<Population,double>("variance"),
        "Find the variance of the distribution of values of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_STDDEV", BuildTraitFunction<Population,double>("stddev"),
        "Find the variance of the distribution of values of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_SUM", BuildTraitFunction<Population,double>("sum"),
        "Add up the total value of a trait (or equation).");
      pop_type.AddMemberFunction("CALC_ENTROPY", BuildTraitFunction<Population,double>("entropy"),
        "Determine the entropy of values for a trait (or equation).");
      pop_type.AddMemberFunction("FIND_MIN",
        [this](Population & pop, const std::string & trait_equation) -> Collection {
          if (pop.GetNumOrgs() == 0) Collection{};
          auto trait_fun =
            BuildTraitSummary<Population,double>(trait_equation, "min_id", pop.GetDataLayout());
          return pop.IteratorAt(trait_fun(pop)).AsPosition();
        },
        "Produce OrgList with just the org with the minimum value of the provided function.");
      pop_type.AddMemberFunction("FIND_MAX",
        [this](Population & pop, const std::string & trait_equation) -> Collection {
          if (pop.GetNumOrgs() == 0) Collection{};
          auto trait_fun =
            BuildTraitSummary<Population,double>(trait_equation, "max_id", pop.GetDataLayout());
          return pop.IteratorAt(trait_fun(pop)).AsPosition();
        },
        "Produce OrgList with just the org with the minimum value of the provided function.");
      pop_type.AddMemberFunction("FILTER",
        [this](Population & pop, const std::string & trait_equation) -> Collection {
          Collection out_collect;
          if (pop.GetNumOrgs() > 0) { // Only do this work if we actually have organisms!
            auto filter = BuildTraitEquation(pop.GetDataLayout(), trait_equation);
            for (auto it = pop.begin(); it != pop.end(); ++it) {
              if (filter(*it)) out_collect.Insert(it);
            }
          }
          return out_collect;
        },
        "Produce OrgList with just the orgs that pass through the filter criteria.");

      collect_type.AddMemberFunction("TRAIT", BuildTraitFunction<Collection,std::string>("0"),
        "Return the value of the provided trait for the first organism");
      collect_type.AddMemberFunction("CALC_RICHNESS", BuildTraitFunction<Collection,double>("richness"),
        "Count the number of distinct values of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_MODE", BuildTraitFunction<Collection,std::string>("mode"),
        "Identify the most common value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_MEAN", BuildTraitFunction<Collection,double>("mean"),
        "Calculate the average value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_MIN", BuildTraitFunction<Collection,double>("min"),
        "Find the smallest value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_MAX", BuildTraitFunction<Collection,double>("max"),
        "Find the largest value of a trait (or equation).");
      collect_type.AddMemberFunction("ID_MIN", BuildTraitFunction<Collection,double>("min_id"),
        "Find the index of the smallest value of a trait (or equation).");
      collect_type.AddMemberFunction("ID_MAX", BuildTraitFunction<Collection,double>("max_id"),
        "Find the index of the largest value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_MEDIAN", BuildTraitFunction<Collection,double>("median"),
        "Find the 50-percentile value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_VARIANCE", BuildTraitFunction<Collection,double>("variance"),
        "Find the variance of the distribution of values of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_STDDEV", BuildTraitFunction<Collection,double>("stddev"),
        "Find the variance of the distribution of values of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_SUM", BuildTraitFunction<Collection,double>("sum"),
        "Add up the total value of a trait (or equation).");
      collect_type.AddMemberFunction("CALC_ENTROPY", BuildTraitFunction<Collection,double>("entropy"),
        "Determine the entropy of values for a trait (or equation).");
      collect_type.AddMemberFunction("FIND_MIN",
        [this](Collection & collect, const std::string & trait_equation) -> Collection {
          if (collect.IsEmpty()) return Collection{};
          auto trait_fun =
            BuildTraitSummary<Collection,double>(trait_equation, "min_id", collect.GetDataLayout());
          return collect.IteratorAt(trait_fun(collect)).AsPosition();
        },
        "Produce OrgList with just the org with the minimum value of the provided function.");
      collect_type.AddMemberFunction("FIND_MAX",
        [this](Collection & collect, const std::string & trait_equation) -> Collection {
          if (collect.IsEmpty()) return Collection{};
          auto trait_fun =
            BuildTraitSummary<Collection,double>(trait_equation, "max_id", collect.GetDataLayout());
          return collect.IteratorAt(trait_fun(collect)).AsPosition();
        },
        "Produce OrgList with just the org with the minimum value of the provided function.");

      // ------ DEPRECATED FUNCTION NAMES ------
      Deprecate("EVAL", "EXEC");
      Deprecate("exit", "EXIT");
      Deprecate("inject", "INJECT");
      Deprecate("print", "PRINT");

      // Add other built-in functions to the config file.
      AddFunction("EXIT", [this](){ control.RequestExit(); return 0; }, "Exit from this MABE run.");
      AddFunction("GET_UPDATE", [this](){ return control.GetUpdate(); }, "Get current update.");
      AddFunction("GET_VERBOSE", [this](){ return control.GetVerbose(); }, "Has the verbose flag been set?");

      std::function<std::string(const std::string &)> preprocess_fun =
        [this](const std::string & str) { return Preprocess(str); };
      AddFunction("PP", preprocess_fun, "Preprocess a string (replacing any ${...} with result.)");

      // Add in built-in event triggers; these are used to indicate when events should happen.
      AddSignal("START");   // Triggered at the beginning of a run.
      AddSignal("UPDATE");  // Tested every update.
    }


    void Deprecate(const std::string & old_name, const std::string & new_name) {
      auto dep_fun = [this,old_name,new_name](const emp::vector<emp::Ptr<emplode::Symbol>> &){
          std::cerr << "Function '" << old_name << "' deprecated; use '" << new_name << "'\n";
          control.RequestExit();
          return 0;      
        };

      AddFunction(old_name, dep_fun, std::string("Deprecated.  Use: ") + new_name);
    }
    
  public:
    MABEScript(MABEBase & in) : control(in) { Initialize(); }
    ~MABEScript() { }

  };

}

#endif
