#include "ExternalMappingMatch.hpp"
#include "Algo.hpp"

#include "IModel.hpp"
#include "VersionRelation.hpp"
#include "json.hpp"
#include "Helpers.h"
#include "Yatools.hpp"

#include <utility>
#include <memory>
#include <chrono>
#include <fstream>
#include <iostream>
#include <unordered_map>

#ifdef _MSC_VER
#   include <filesystem>
#else
#   include <experimental/filesystem>
#endif

using namespace std;
using namespace std::experimental;

#define LOG(LEVEL, FMT, ...) CONCAT(YALOG_, LEVEL)("external", (FMT), ## __VA_ARGS__)

using json = nlohmann::json;

namespace yadiff
{
  struct ExternalMappingEntry
  {
      offset_t      src;
      offset_t      dst;
      std::string   comment;

      ExternalMappingEntry(offset_t s_, offset_t d_, std::string c_): src(s_), dst(d_), comment(c_){}
  };

class ExternalMappingMatchAlgo: public IDiffAlgo
{
public:
    virtual ~ExternalMappingMatchAlgo(){}

    ExternalMappingMatchAlgo(const AlgoCfg& config);

    /*
     * prepares input signature databases
     */
    virtual bool Prepare(const IModel& db1, const IModel& db2);

    /*
     * uses previously registered signature databases and input version relations
     * to compute a new version relation vector
     */
    virtual bool Analyse(const OnRelationFn& output, const RelationWalkerfn& input);

    virtual const char* GetName() const;
private:

    const IModel* pDb1_;
    const IModel* pDb2_;
    const AlgoCfg config_;
    std::vector<ExternalMappingEntry>mapping_;
};

std::shared_ptr<IDiffAlgo> MakeExternalMappingMatchAlgo(const AlgoCfg& config)
{
    return std::make_shared<ExternalMappingMatchAlgo>(config);
}



const char* ExternalMappingMatchAlgo::GetName() const{
    return "ExternalMappingMatchAlgo";
}

ExternalMappingMatchAlgo::ExternalMappingMatchAlgo(const AlgoCfg& config):
        pDb1_(nullptr),
        pDb2_(nullptr),
        config_(config)
{

}

bool ExternalMappingMatchAlgo::Prepare(const IModel& db1, const IModel& db2)
{
    pDb1_ = &db1;
    pDb2_ = &db2;


    // load JSON mapping
    if(!filesystem::exists(filesystem::path(config_.ExternalMappingMatch.MappingFilePath)))
      {
        LOG(ERROR, "json file %s does not exist\n", config_.ExternalMappingMatch.MappingFilePath);
        return false;
      }
    std::ifstream file(config_.ExternalMappingMatch.MappingFilePath);
    json data = json::parse(file);
    for(auto it = data.begin(); it != data.end(); ++it)
      {
        const auto element = *it;
        auto it_src = element.find("src");
        if(it_src == element.end())
          {
            LOG(WARNING, "invalid JSON entry, no src in it\n");
            continue;
          }
        auto it_dst = element.find("dst");
        if(it_dst == element.end())
        {
            LOG(WARNING, "JSON entry, no dst in it\n");
            continue;
          }
        auto it_comment = element.find("comment");
        if(it_comment == element.end())
        {
            LOG(WARNING, "invalid JSON entry, no comment in it\n");
            continue;
          }
        mapping_.emplace_back(ExternalMappingEntry{*it_src, *it_dst, *it_comment});
      }


    return true;
}

bool ExternalMappingMatchAlgo::Analyse(const OnRelationFn& output, const RelationWalkerfn& input)
{
    UNUSED(input);

    if(nullptr == pDb1_)
        return false;

    if(nullptr == pDb2_)
        return false;

//    std::vector<offset_t> src_offset_to_treat;
    std::unordered_map<offset_t, offset_t> offset_to_treat;
    for(const auto entry : mapping_)
      {
        offset_to_treat.emplace(std::make_pair(entry.src, entry.dst));
//        src_offset_to_treat.push_back(entry.src);
      }

    Relation relation;
    memset(&relation, 0, sizeof relation);
    if(config_.ExternalMappingMatch.CustomRelationConfidence)
        relation.confidence_ = config_.ExternalMappingMatch.RelationConfidence;
    else
      relation.confidence_ = RELATION_CONFIDENCE_GOOD;
    relation.direction_ = RELATION_DIRECTION_BOTH;

    pDb1_->walk([&](const HVersion& src_version)
    {
        const auto match = offset_to_treat.find(src_version.address());
        if(match == offset_to_treat.end())
            return WALK_CONTINUE;

        // src address found, stat looking for dst address
        pDb2_->walk([&](const HVersion& dst_version)
        {
            if(match->second != dst_version.address())
                return WALK_CONTINUE;

            // check signatures
            relation.type_ = RELATION_TYPE_DIFF;
            bool sign_found = false;
            src_version.walk_signatures([&](const HSignature& src_sign)
            {
                dst_version.walk_signatures([&](const HSignature& dst_sign)
                {
                    if(src_sign == dst_sign)
                    {
                        sign_found = true;
                        relation.type_ = RELATION_TYPE_EXACT_MATCH;
                        return WALK_STOP;
                    }
                    return WALK_CONTINUE;
                });
                return sign_found ? WALK_STOP : WALK_CONTINUE;
            });
            relation.version1_ = src_version;
            relation.version2_ = dst_version;
            output(relation);
            offset_to_treat.erase(match->first);
            return WALK_STOP;
        });

        // if there is no more mapping to do, stop iterating
        return offset_to_treat.empty() ? WALK_STOP : WALK_CONTINUE;
    });

    return true;
}
}
