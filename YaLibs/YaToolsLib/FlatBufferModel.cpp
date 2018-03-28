//  Copyright (C) 2017 The YaCo Authors
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "FlatBufferModel.hpp"

#include "HVersion.hpp"
#include "IModelVisitor.hpp"
#include "HObject.hpp"
#include "FileUtils.hpp"
#include "Logger.h"
#include "Yatools.h"
#include "ModelIndex.hpp"

#ifdef DEBUG
#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#endif

#include <flatbuffers/flatbuffers.h>
#include <yadb_generated.h>

namespace fb = flatbuffers;

#include <chrono>

#if 0
#define HAS_FLATBUFFER_LOGGING
#define LOG(LEVEL, FMT, ...) CONCAT(YALOG_, LEVEL)("flatbuffer", (FMT), ## __VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif

namespace
{
static YaToolObjectType_e get_object_type(yadb::ObjectType value)
{
    switch(value)
    {
        case yadb::ObjectType_Unknown:          return OBJECT_TYPE_UNKNOWN;
        case yadb::ObjectType_Binary:           return OBJECT_TYPE_BINARY;
        case yadb::ObjectType_Data:             return OBJECT_TYPE_DATA;
        case yadb::ObjectType_Code:             return OBJECT_TYPE_CODE;
        case yadb::ObjectType_Function:         return OBJECT_TYPE_FUNCTION;
        case yadb::ObjectType_Struct:           return OBJECT_TYPE_STRUCT;
        case yadb::ObjectType_Enum:             return OBJECT_TYPE_ENUM;
        case yadb::ObjectType_EnumMember:       return OBJECT_TYPE_ENUM_MEMBER;
        case yadb::ObjectType_BasicBlock:       return OBJECT_TYPE_BASIC_BLOCK;
        case yadb::ObjectType_Segment:          return OBJECT_TYPE_SEGMENT;
        case yadb::ObjectType_SegmentChunk:     return OBJECT_TYPE_SEGMENT_CHUNK;
        case yadb::ObjectType_StructMember:     return OBJECT_TYPE_STRUCT_MEMBER;
        case yadb::ObjectType_StackFrame:       return OBJECT_TYPE_STACKFRAME;
        case yadb::ObjectType_StackFrameMember: return OBJECT_TYPE_STACKFRAME_MEMBER;
        case yadb::ObjectType_ReferenceInfo:    return OBJECT_TYPE_REFERENCE_INFO;
    }
    return OBJECT_TYPE_UNKNOWN;
}

static CommentType_e get_comment_type(yadb::CommentType value)
{
    switch(value)
    {
        case yadb::CommentType_Unknown:         return COMMENT_UNKNOWN;
        case yadb::CommentType_Repeatable:      return COMMENT_REPEATABLE;
        case yadb::CommentType_NonRepeatable:   return COMMENT_NON_REPEATABLE;
        case yadb::CommentType_Anterior:        return COMMENT_ANTERIOR;
        case yadb::CommentType_Posterior:       return COMMENT_POSTERIOR;
        case yadb::CommentType_Bookmark:        return COMMENT_BOOKMARK;
    }
    return COMMENT_UNKNOWN;
}

static SignatureAlgo_e get_signature_algo(yadb::HashType value)
{
    switch(value)
    {
        case yadb::HashType_Unknown:    return SIGNATURE_ALGORITHM_UNKNOWN;
        case yadb::HashType_None:       return SIGNATURE_ALGORITHM_NONE;
        case yadb::HashType_Crc32:      return SIGNATURE_ALGORITHM_CRC32;
        case yadb::HashType_Md5:        return SIGNATURE_ALGORITHM_MD5;
    }
    return SIGNATURE_ALGORITHM_UNKNOWN;
}

static SignatureMethod_e get_signature_method(yadb::SignatureMethod value)
{
    switch(value)
    {
        case yadb::SignatureMethod_Unknown:     return SIGNATURE_UNKNOWN;
        case yadb::SignatureMethod_FirstByte:   return SIGNATURE_FIRSTBYTE;
        case yadb::SignatureMethod_Full:        return SIGNATURE_FULL;
        case yadb::SignatureMethod_Invariants:  return SIGNATURE_INVARIANTS;
        case yadb::SignatureMethod_OpCode:      return SIGNATURE_OPCODE_HASH;
        case yadb::SignatureMethod_IntraGraph:  return SIGNATURE_INTRA_GRAPH_HASH;
        case yadb::SignatureMethod_String:      return SIGNATURE_STRING_HASH;
    }
    return SIGNATURE_UNKNOWN;
}

struct ObjectCtx
{
    YaToolObjectId      id;
    HObject_id_t        idx;
    YaToolObjectType_e  type;
    HVersion_id_t       version_id;
    uint32_t            xrefs_to_idx;
};
STATIC_ASSERT_POD(ObjectCtx);

struct VersionCtx
{
    const yadb::Version*    version;
    HObject_id_t            object_id;
    HSignature_id_t         sig_id;
};
STATIC_ASSERT_POD(VersionCtx);

struct SignatureCtx
{
    const yadb::Signature*  signature;
    HVersion_id_t           version_id;
};
STATIC_ASSERT_POD(SignatureCtx);

struct FlatBufferModel;

struct ViewObjects : public IObjects
{
    ViewObjects(FlatBufferModel& db)
        : db_(db)
    {
    }

    // IObjects methods
    void                accept          (HObject_id_t object_id, IModelVisitor& visitor) const override;
    YaToolObjectType_e  type            (HObject_id_t object_id) const override;
    YaToolObjectId      id              (HObject_id_t object_id) const override;
    bool                has_signature   (HObject_id_t object_id) const override;
    void                walk_versions   (HObject_id_t object_id, const OnVersionFn& fnWalk) const override;
    void                walk_xrefs_from (HObject_id_t object_id, const OnXrefFromFn& fnWalk) const override;
    void                walk_xrefs_to   (HObject_id_t object_id, const OnObjectFn& fnWalk) const override;
    bool                match           (HObject_id_t object_id, const HObject& remote) const override;

    FlatBufferModel&    db_;
};

struct ViewVersions : public IVersions
{
    ViewVersions(FlatBufferModel& db)
        : db_(db)
    {
    }

    // IVersions methods
    void                accept(HVersion_id_t version_id, IModelVisitor& visitor) const override;

    YaToolObjectId      id              (HVersion_id_t version_id) const override;
    YaToolObjectId      parent_id       (HVersion_id_t version_id) const override;
    offset_t            size            (HVersion_id_t version_id) const override;
    YaToolObjectType_e  type            (HVersion_id_t version_id) const override;
    offset_t            address         (HVersion_id_t version_id) const override;
    const_string_ref    username        (HVersion_id_t version_id) const override;
    int                 username_flags  (HVersion_id_t version_id) const override;
    const_string_ref    prototype       (HVersion_id_t version_id) const override;
    YaToolFlag_T        flags           (HVersion_id_t version_id) const override;
    int                 string_type     (HVersion_id_t version_id) const override;
    const_string_ref    header_comment  (HVersion_id_t version_id, bool repeatable) const override;

    void                walk_signatures         (HVersion_id_t version_id, const OnSignatureFn& fnWalk) const override;
    void                walk_xrefs_from         (HVersion_id_t version_id, const OnXrefFromFn& fnWalk) const override;
    void                walk_xrefs_to           (HVersion_id_t version_id, const OnObjectFn& fnWalk) const override;
    void                walk_blobs              (HVersion_id_t version_id, const OnBlobFn& fnWalk) const override;
    void                walk_comments           (HVersion_id_t version_id, const OnCommentFn& fnWalk) const override;
    void                walk_value_views        (HVersion_id_t version_id, const OnValueViewFn& fnWalk) const override;
    void                walk_register_views     (HVersion_id_t version_id, const OnRegisterViewFn& fnWalk) const override;
    void                walk_hidden_areas       (HVersion_id_t version_id, const OnHiddenAreaFn& fnWalk) const override;
    void                walk_xrefs              (HVersion_id_t version_id, const OnXrefFn& fnWalk) const override;
    void                walk_xref_attributes    (HVersion_id_t version_id, const XrefAttributes* hattr, const OnAttributeFn& fnWalk) const override;
    void                walk_attributes         (HVersion_id_t version_id, const OnAttributeFn& fnWalk) const override;

    FlatBufferModel&    db_;
};

struct ViewSignatures : public ISignatures
{
    ViewSignatures(FlatBufferModel& db)
        : db_(db)
    {
    }

    // ISignatures methods
    Signature get(HSignature_id_t sig_id) const override;

    FlatBufferModel& db_;
};

struct FlatBufferModel : public IModel
{
    FlatBufferModel(const std::shared_ptr<Mmap_ABC>& mmap);

    // private methods
    void setup();

    // IModelAccept methods
    void                accept(IModelVisitor& visitor) override;

    // IModel methods
    void                walk_objects                    (const OnObjectAndIdFn& fnWalk) const override;
    size_t              num_objects                     () const override;
    void                walk_objects_with_signature     (const HSignature& hash, const OnObjectFn& fnWalk) const override;
    size_t              num_objects_with_signature      (const HSignature& hash) const override;
    void                walk_versions_with_signature    (const HSignature& hash, const OnVersionFn& fnWalk) const override;
    HObject             get_object                      (YaToolObjectId id) const override;
    bool                has_object                      (YaToolObjectId id) const override;
    void                walk_versions_without_collision (const OnSigAndVersionFn& fnWalk) const override;
    void                walk_matching_versions          (const HObject& object, size_t min_size, const OnVersionPairFn& fnWalk) const override;

    std::shared_ptr<Mmap_ABC>   buffer_;
    const yadb::Root*           root_;
    std::vector<ObjectCtx>      objects_;
    std::vector<VersionCtx>     versions_;
    std::vector<SignatureCtx>   signatures_;
    ModelIndex                  index_;
    ViewObjects                 view_objects_;
    ViewVersions                view_versions_;
    ViewSignatures              view_signatures_;
};

const char               gEmpty[] = "";
const const_string_ref   gEmptyRef = {gEmpty, sizeof gEmpty - 1};

const_string_ref make_string_ref_from(const fb::String* value)
{
    if(!value)
        return gEmptyRef;
    return const_string_ref{value->data(), value->size()};
}
}

std::shared_ptr<IModel> MakeFlatBufferModel(const std::shared_ptr<Mmap_ABC>& mmap)
{
    return std::make_shared<FlatBufferModel>(mmap);
}

std::shared_ptr<IModel> MakeFlatBufferModel(const std::string& filename)
{
    return MakeFlatBufferModel(MmapFile(filename.data()));
}

#ifndef NDEBUG
namespace
{
bool ValidateFlatBuffer(const void* data, size_t szdata)
{
    LOG(INFO, "verify flatbuffer\n");
    if(!yadb::RootBufferHasIdentifier(data))
        return false;
    flatbuffers::Verifier v(reinterpret_cast<const uint8_t*>(data), szdata, 64, 20000000);
    const bool reply = yadb::VerifyRootBuffer(v);
    return reply;
}
}
#endif

FlatBufferModel::FlatBufferModel(const std::shared_ptr<Mmap_ABC>& mmap)
    : buffer_           (mmap)
    , root_             (nullptr)
    , view_objects_     (*this)
    , view_versions_    (*this)
    , view_signatures_  (*this)
{
    assert(buffer_->Get());
    assert(yadb::RootBufferHasIdentifier(buffer_->Get()));
    root_ = yadb::GetRoot(buffer_->Get());
    assert(ValidateFlatBuffer(buffer_->Get(), buffer_->GetSize()));
    setup();
}


template<typename T, typename U>
static void walk_stoppable(const T* pdata, const U& operand)
{
    if(pdata)
        for(const auto& value : *pdata)
            if(operand(value) == WALK_STOP)
                return;
}

template<typename T, typename U>
static void walk(const T* pdata, const U& operand)
{
    if(pdata)
        for(const auto& value : *pdata)
            operand(value);
}

template<typename T>
static size_t get_size(const T* pdata)
{
    if(pdata)
        return pdata->size();
    return 0;
}

static const fb::Vector<fb::Offset<yadb::Version>>* get_versions_from(const FlatBufferModel& db, YaToolObjectType_e type)
{
    switch(type)
    {
        case OBJECT_TYPE_COUNT:
        case OBJECT_TYPE_UNKNOWN:           return nullptr;
        case OBJECT_TYPE_BINARY:            return db.root_->binaries();
        case OBJECT_TYPE_SEGMENT:           return db.root_->segments();
        case OBJECT_TYPE_SEGMENT_CHUNK:     return db.root_->segment_chunks();
        case OBJECT_TYPE_STRUCT:            return db.root_->structs();
        case OBJECT_TYPE_STRUCT_MEMBER:     return db.root_->struct_members();
        case OBJECT_TYPE_ENUM:              return db.root_->enums();
        case OBJECT_TYPE_ENUM_MEMBER:       return db.root_->enum_members();
        case OBJECT_TYPE_FUNCTION:          return db.root_->functions();
        case OBJECT_TYPE_STACKFRAME:        return db.root_->stackframes();
        case OBJECT_TYPE_STACKFRAME_MEMBER: return db.root_->stackframe_members();
        case OBJECT_TYPE_DATA:              return db.root_->datas();
        case OBJECT_TYPE_CODE:              return db.root_->codes();
        case OBJECT_TYPE_REFERENCE_INFO:    return db.root_->reference_infos();
        case OBJECT_TYPE_BASIC_BLOCK:       return db.root_->basic_blocks();
    }
    return nullptr;
}

template<typename T>
static void walk_all_version_arrays(FlatBufferModel& db, const T& operand)
{
    for(const auto type : ordered_types)
        operand(get_versions_from(db, type), type);
}

template<typename T>
static void walk_all_versions(FlatBufferModel& db, const T& operand)
{
    walk_all_version_arrays(db, [&](const auto* values, YaToolObjectType_e type)
    {
        walk(values, [&](const auto* value)
        {
            operand(value, type);
        });
    });
}

static const_string_ref string_from(const FlatBufferModel& db, uint32_t index)
{
    return make_string_ref_from(db.root_->strings()->Get(index));
}

static void parse_objects(FlatBufferModel& db)
{
    // create object contexts
    LOG(INFO, "parsing %zd 0x%zx objects\n", db.objects_.capacity(), db.objects_.capacity());
    walk(db.root_->objects(), [&](const yadb::Object* object)
    {
        const auto idx = static_cast<HObject_id_t>(db.objects_.size());
        const auto id = object->id();
        // type & version_id will be patched later on
        db.objects_.push_back({id, idx, get_object_type(object->type()), ~0u, ~0u});
        return WALK_CONTINUE;
    });

    // we want to deliver objects ordered by types
    LOG(INFO, "sort objects\n");
    std::sort(db.objects_.begin(), db.objects_.end(), [&](const ObjectCtx& a, const auto& b)
    {
        const auto get = [&](const auto &v) { return std::make_pair(indexed_types[v.type], v.id); };
        return get(a) < get(b);
    });

    // generate our object indexes
    LOG(INFO, "index objects\n");
    HObject_id_t obj_idx = 0;
    for(auto& obj : db.objects_)
    {
        obj.idx = obj_idx;
        add_object(db.index_, obj.id, obj_idx);
        ++obj_idx;
    }
    finish_objects(db.index_);
}

static void parse_versions(FlatBufferModel& db)
{
    // create version contexts
    LOG(INFO, "parse versions\n");
    
    SigMap sigmap;
    walk_all_versions(db, [&](const yadb::Version* version, YaToolObjectType_e type)
    {
        const auto version_id = static_cast<HVersion_id_t>(db.versions_.size());
        const auto object_id = find_object_id(db.index_, version->object_id());
        if(!object_id)
            return;

        if(*object_id >= db.objects_.size())
            return;

        auto& object = db.objects_[*object_id];
        assert(object.type == type); UNUSED(type);
        object.version_id = std::min(object.version_id, version_id);
        db.versions_.push_back({version, *object_id, ~0u});

        walk(version->xrefs(), [&](const yadb::Xref* xref)
        {
            add_xref_to(db.index_, *object_id, xref->id());
        });

        auto& verctx = db.versions_.back();
        walk(version->signatures(), [&](const yadb::Signature* signature)
        {
            const auto sig_id = static_cast<HSignature_id_t>(db.signatures_.size());
            verctx.sig_id = std::min(verctx.sig_id, sig_id);
            db.signatures_.push_back({signature, version_id});
            const auto key = string_from(db, signature->value());
            add_sig(db.index_, sigmap, key, sig_id);
        });
    });

    LOG(INFO, "index signatures\n");
    finish_sigs(db.index_, sigmap);
}

static void index_xrefs(FlatBufferModel& db)
{
    // create xrefs_to contexts
    LOG(INFO, "index xrefs\n");
    finish_xrefs(db.index_, [&](HObject_id_t to, uint32_t xref_to_idx)
    {
        auto& obj = db.objects_[to];
        assert(obj.idx == to);
        auto& idx = obj.xrefs_to_idx;
        idx = std::min(idx, xref_to_idx);
    });
}

void FlatBufferModel::setup()
{
    LOG(INFO, "initialize model\n");
    const size_t num_objects = get_size(root_->objects());
    size_t num_versions = 0;
    walk_all_version_arrays(*this, [&](const auto* values, YaToolObjectType_e type)
    {
        UNUSED(type);
        num_versions += values ? values->size() : 0;
    });

    objects_.reserve(num_objects);
    versions_.reserve(num_versions);
    signatures_.reserve(num_versions);
    reserve(index_, num_objects, num_versions);

    parse_objects(*this);
    parse_versions(*this);
    index_xrefs(*this);

    // ensure we correctly precomputed capacity
    assert(num_objects >= objects_.size());
    assert(num_objects <= 64 || num_objects >= objects_.capacity());
    assert(num_versions == versions_.size());
    const auto print_size = [&](const char* name, const auto& d)
    {
        UNUSED(name);
        UNUSED(d);
        LOG(INFO, "%s %zd elements x %zd bytes = %zd KB\n", name, d.size(), sizeof d[0], (d.size() * sizeof d[0]) / 1000);
    };
    print_size("objects", objects_);
    print_size("versions", versions_);
    print_size("signatures", signatures_);
    print_size("sigs", index_.sigs_);
    print_size("unique_sigs", index_.unique_sigs_);
    print_size("xrefs_to", index_.xrefs_to_);
    print_size("object_ids", index_.object_ids_);
}

namespace
{
template<typename T>
void walk_versions(const FlatBufferModel& db, const ObjectCtx& object, const T& operand)
{
    const auto end = db.versions_.size();
    for(auto i = object.version_id; i < end; ++i)
    {
        const auto& version = db.versions_[i];
        if(object.idx != version.object_id)
            break;
        if(operand(i, version) == WALK_STOP)
            break;
    }
}

template<typename T>
void walk_xrefs_to(const FlatBufferModel& db, const ObjectCtx& object, const T& operand)
{
    walk_xrefs(db.index_, object.idx, object.xrefs_to_idx, operand);
}

template<typename T>
void walk_signatures(const FlatBufferModel& db, const VersionCtx& ctx, const T& operand)
{
    optional<HVersion_id_t> version_id;
    const auto end = db.signatures_.size();
    for(auto i = ctx.sig_id; i < end; ++i)
    {
        const auto& sig = db.signatures_[i];
        if(version_id && *version_id != sig.version_id)
            return;
        version_id = sig.version_id;
        if(operand(i, sig) == WALK_STOP)
            return;
    }
}

template<typename T>
void walk_xrefs(const FlatBufferModel&, const VersionCtx& ctx, const T& operand)
{
    walk_stoppable(ctx.version->xrefs(), [&](const yadb::Xref* xref)
    {
        return operand(xref);
    });
}

void accept_version(const FlatBufferModel& db, const VersionCtx& ctx, IModelVisitor& visitor)
{
    const auto* version = ctx.version;
    visitor.visit_start_object_version();
    visitor.visit_size(version->size());
    visitor.visit_parent_id(version->parent_id());
    visitor.visit_address(version->address());

    const auto* username = version->username();
    if(username)
        visitor.visit_name(string_from(db, username->value()), username->flags());

    const auto prototype = version->prototype();
    if(prototype)
        visitor.visit_prototype(string_from(db, prototype));

    visitor.visit_flags(version->flags());

    const auto string_type = version->string_type();
    if(string_type != UINT8_MAX)
        visitor.visit_string_type(string_type);

    const auto get_values = [](const auto* value)
    {
        return value && value->size() ? value : nullptr;
    };

    // signatures
    visitor.visit_start_signatures();
    walk_signatures(db, ctx, [&](HSignature_id_t id, const SignatureCtx& sig)
    {
        UNUSED(id);
        const auto s      = sig.signature;
        const auto method = get_signature_method(s->method());
        const auto algo   = get_signature_algo(s->type());
        visitor.visit_signature(method, algo, string_from(db, s->value()));
        return WALK_CONTINUE;
    });
    visitor.visit_end_signatures();

    auto comment = version->header_comment_repeatable();
    if(comment)
        visitor.visit_header_comment(true, string_from(db, comment));

    comment = version->header_comment_nonrepeatable();
    if(comment)
        visitor.visit_header_comment(false, string_from(db, comment));

    // offsets
    const auto* comments = get_values(version->comments());
    const auto* valueviews = get_values(version->valueviews());
    const auto* registerviews = get_values(version->registerviews());
    const auto* hiddenareas = get_values(version->hiddenareas());
    if(comments || valueviews || registerviews || hiddenareas)
    {
        visitor.visit_start_offsets();
        walk(comments, [&](const auto* comment)
        {
            visitor.visit_offset_comments(comment->offset(), get_comment_type(comment->type()), string_from(db, comment->value()));
        });
        walk(valueviews, [&](const auto* view)
        {
            visitor.visit_offset_valueview(view->offset(), view->operand(), string_from(db, view->value()));
        });
        walk(registerviews, [&](const auto* view)
        {
            visitor.visit_offset_registerview(view->offset(), view->end_offset(), string_from(db, view->register_name()), string_from(db, view->register_new_name()));
        });
        walk(hiddenareas, [&](const auto* area)
        {
            visitor.visit_offset_hiddenarea(area->offset(), area->area_size(), string_from(db, area->value()));
        });
        visitor.visit_end_offsets();
    }

    // xrefs
    visitor.visit_start_xrefs();
    walk_xrefs(db, ctx, [&](const yadb::Xref* xref)
    {
        const auto xref_id = xref->id();
        visitor.visit_start_xref(xref->offset(), xref_id, xref->operand());
        walk(xref->attributes(), [&](const auto* attribute)
        {
            visitor.visit_xref_attribute(string_from(db, attribute->key()), string_from(db, attribute->value()));
        });
        visitor.visit_end_xref();
        return WALK_CONTINUE;
    });
    visitor.visit_end_xrefs();

    // attributes
    walk(version->attributes(), [&](const auto* attribute)
    {
        visitor.visit_attribute(string_from(db, attribute->key()), string_from(db, attribute->value()));
    });

    // blobs
    walk(version->blobs(), [&](const auto* blob)
    {
        const auto data = blob->data();
        visitor.visit_blob(blob->offset(), data->data(), data->size());
    });

    visitor.visit_end_object_version();
}

void accept_object(const FlatBufferModel& db, const ObjectCtx& object, IModelVisitor& visitor)
{
    visitor.visit_start_reference_object(object.type);
    visitor.visit_id(object.id);
    walk_versions(db, object, [&](HVersion_id_t id, const VersionCtx& ctx)
    {
        UNUSED(id);
        accept_version(db, ctx, visitor);
        return WALK_CONTINUE;
    });
    visitor.visit_end_reference_object();
}

template<typename T>
struct ProgressLogger
{
    ProgressLogger(size_t max, const T& now)
        : max(max)
        , i(0)
        , last_progress(~0u)
        , last_chunk(0)
        , last_type(OBJECT_TYPE_COUNT)
        , last_clock(now)
    {
        LOG(INFO, "accept all objects (%zd)\n", max);
    }

    void Update(YaToolObjectType_e type)
    {
        const auto progress = (i * 100) / max;
        if(last_progress != progress || last_type != type)
        {
            const auto now = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_clock).count();
            UNUSED(duration);
            LOG(INFO, "accept %s %zd%% %" PRIx64 " obj/s\n", get_object_type_string(type), progress, (last_chunk * 1000) / (duration ? duration : 1));
            last_chunk = 0;
            last_clock = now;
        }
        last_progress = progress;
        last_type = type;
        ++i;
        ++last_chunk;
    }

    size_t max;
    size_t i;
    size_t last_progress;
    size_t last_chunk;
    YaToolObjectType_e last_type;
    T last_clock;
};

template<typename T>
ProgressLogger<T> MakeProgressLogger(size_t max, const T& now)
{
    return ProgressLogger<T>(max, now);
}

#ifdef HAS_FLATBUFFER_LOGGING
#define DECLARE_PROGRESS_LOGGER(SIZE) auto progress = MakeProgressLogger(SIZE, std::chrono::high_resolution_clock::now())
#define UPDATE_PROGRESS_LOGGER(TYPE) progress.Update(TYPE)
#else
#define DECLARE_PROGRESS_LOGGER(...) do{} while(0)
#define UPDATE_PROGRESS_LOGGER(...) do{} while(0)
#endif
}

void FlatBufferModel::accept(IModelVisitor& visitor)
{
    DECLARE_PROGRESS_LOGGER(objects_.size());
    visitor.visit_start();
    for(const auto& object : objects_)
    {
        accept_object(*this, object, visitor);
        UPDATE_PROGRESS_LOGGER(object.type);
    }
    visitor.visit_end();
}

void FlatBufferModel::walk_objects(const OnObjectAndIdFn& fnWalk) const
{
    const auto end = static_cast<HObject_id_t>(objects_.size());
    for(HObject_id_t id = 0; id < end; ++id)
        if(fnWalk(objects_[id].id, {&view_objects_, id}) == WALK_STOP)
            return;
}

size_t FlatBufferModel::num_objects() const
{
    return objects_.size();
}

void FlatBufferModel::walk_objects_with_signature(const HSignature& hash, const OnObjectFn& fnWalk) const
{
    walk_sigs(index_, make_string_ref(hash.get()), [&](const Sig& sig)
    {
        return fnWalk({&view_objects_, versions_[signatures_[sig.idx].version_id].object_id});
    });
}

size_t FlatBufferModel::num_objects_with_signature(const HSignature& hash) const
{
    return num_sigs(index_, make_string_ref(hash.get()));
}

void FlatBufferModel::walk_versions_with_signature(const HSignature& hash, const OnVersionFn& fnWalk) const
{
    walk_sigs(index_, make_string_ref(hash.get()), [&](const Sig& sig)
    {
        return fnWalk({&view_versions_, signatures_[sig.idx].version_id});
    });
}

HObject FlatBufferModel::get_object(YaToolObjectId id) const
{
    if(const auto object_id = find_object_id(index_, id))
        return{&view_objects_, *object_id};
    return{nullptr, 0};
}

bool FlatBufferModel::has_object(YaToolObjectId id) const
{
    return !!find_object_id(index_, id);
}

void FlatBufferModel::walk_versions_without_collision(const OnSigAndVersionFn& fnWalk) const
{
    walk_all_unique_sigs(index_, [&](const Sig& sig)
    {
        return fnWalk({&view_signatures_, sig.idx}, {&view_versions_, signatures_[sig.idx].version_id});
    });
}

void FlatBufferModel::walk_matching_versions(const HObject& object, size_t min_size, const OnVersionPairFn& fnWalk) const
{
    // iterate over remote objection versions
    object.walk_versions([&](const HVersion& remoteVersion)
    {
        //iterate over remote signatures
        ContinueWalking_e stop_current_iteration = WALK_CONTINUE;
        remoteVersion.walk_signatures([&](const HSignature& remote)
        {
            walk_sigs(index_, make_string_ref(remote.get()), [&](const Sig& sig)
            {
                const auto version_id = signatures_[sig.idx].version_id;
                const auto& version = versions_[version_id].version;
                if(version->size() != remoteVersion.size())
                    return WALK_CONTINUE;
                if(!is_unique_sig(index_, sig.key) && version->size() < min_size)
                    return WALK_CONTINUE;
                if(fnWalk({&view_versions_, version_id}, remoteVersion) != WALK_STOP)
                    return WALK_CONTINUE;
                stop_current_iteration = WALK_STOP;
                return stop_current_iteration;
            });
            return stop_current_iteration;
        });
        return stop_current_iteration;
    });
}

void ViewObjects::accept(HObject_id_t object_id, IModelVisitor& visitor) const
{
    accept_object(db_, db_.objects_[object_id], visitor);
}

YaToolObjectType_e ViewObjects::type(HObject_id_t object_id) const
{
    return db_.objects_[object_id].type;
}

YaToolObjectId ViewObjects::id(HObject_id_t object_id) const
{
    return db_.objects_[object_id].id;
}

bool ViewObjects::has_signature(HObject_id_t object_id) const
{
    const auto& object = db_.objects_[object_id];
    const auto& version = db_.versions_[object.version_id];
    const auto sigs = version.version->signatures();
    return sigs && sigs->size();
}

void ViewObjects::walk_versions(HObject_id_t object_id, const OnVersionFn& fnWalk) const
{
    ::walk_versions(db_, db_.objects_[object_id], [&](HVersion_id_t id, const VersionCtx&)
    {
        return fnWalk({&db_.view_versions_, id});
    });
}

void ViewObjects::walk_xrefs_from(HObject_id_t object_id, const OnXrefFromFn& fnWalk) const
{
    ::walk_versions(db_, db_.objects_[object_id], [&](HVersion_id_t, const VersionCtx& version)
    {
        ContinueWalking_e stop = WALK_CONTINUE;
        walk_xrefs(db_, version, [&](const yadb::Xref* xref)
        {
            const auto object = db_.get_object(xref->id());
            if(object.is_valid())
                stop = fnWalk(xref->offset(), xref->operand(), object);
            return stop;
        });
        return stop;
    });
}

void ViewObjects::walk_xrefs_to(HObject_id_t object_id, const OnObjectFn& fnWalk) const
{
    ::walk_xrefs_to(db_, db_.objects_[object_id], [&](HObject_id_t from)
    {
        return fnWalk({this, from});
    });
}

bool ViewObjects::match(HObject_id_t object_id, const HObject& remote_object) const
{
    bool match_found = false;
    //walk on versions of the remote DB
    remote_object.walk_versions([&](const HVersion& remove_version)
    {
        //walk on versions of this FB db
        ::walk_versions(db_, db_.objects_[object_id], [&](const HSignature_id_t, const VersionCtx& local)
        {
            if(local.version->size() != remove_version.size())
                return WALK_STOP;

            bool this_match_found = true;
            //We will effectively have a match if all the signatures of this DB match with one signature of the remote DB

            //Look for all the signatures of the remote DB
            int local_count = 0;
            int remote_count = 0;
            remove_version.walk_signatures([&](const HSignature& signature)
            {
                const auto signref = make_string_ref(signature.get());
                local_count++;
                int found = 0;
                remote_count = 0;
                //Look for all our signatures
                walk_signatures(db_, local, [&](HSignature_id_t, const SignatureCtx& sign)
                {
                    remote_count++;
                    found += signref == string_from(db_, sign.signature->value());
                    return WALK_CONTINUE;
                });
                if(found != 1)
                {
                    this_match_found = false;
                }
                return WALK_CONTINUE;
            });

            match_found |= (this_match_found && local_count == remote_count);
            return WALK_CONTINUE;
        });
        if(match_found)
            return WALK_STOP;
        return WALK_CONTINUE;
    });
    return match_found;
}

void ViewVersions::accept(HVersion_id_t version_id, IModelVisitor& visitor) const
{
    accept_version(db_, db_.versions_[version_id], visitor);
}

YaToolObjectId ViewVersions::id(HVersion_id_t version_id) const
{
    return db_.objects_[db_.versions_[version_id].object_id].id;
}

YaToolObjectId ViewVersions::parent_id(HVersion_id_t version_id) const
{
    return db_.versions_[version_id].version->parent_id();
}

offset_t ViewVersions::size(HVersion_id_t version_id) const
{
    return db_.versions_[version_id].version->size();
}

YaToolObjectType_e ViewVersions::type(HVersion_id_t version_id) const
{
    return db_.objects_[db_.versions_[version_id].object_id].type;
}

offset_t ViewVersions::address(HVersion_id_t version_id) const
{
    return db_.versions_[version_id].version->address();
}

const_string_ref ViewVersions::username(HVersion_id_t version_id) const
{
    const auto* username = db_.versions_[version_id].version->username();
    return username ? string_from(db_, username->value()) : gEmptyRef;
}

int ViewVersions::username_flags(HVersion_id_t version_id) const
{
    const auto* username = db_.versions_[version_id].version->username();
    return username ? username->flags() : 0;
}

const_string_ref ViewVersions::prototype(HVersion_id_t version_id) const
{
    return string_from(db_, db_.versions_[version_id].version->prototype());
}

YaToolFlag_T ViewVersions::flags(HVersion_id_t version_id) const
{
    return db_.versions_[version_id].version->flags();
}

int ViewVersions::string_type(HVersion_id_t version_id) const
{
    return db_.versions_[version_id].version->string_type();
}

const_string_ref ViewVersions::header_comment(HVersion_id_t version_id, bool repeatable) const
{
    const auto* version = db_.versions_[version_id].version;
    const auto value = repeatable ? version->header_comment_repeatable() : version->header_comment_nonrepeatable();
    return string_from(db_, value);
}

void ViewVersions::walk_signatures(HVersion_id_t version_id, const OnSignatureFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id];
    ::walk_signatures(db_, version, [&](HSignature_id_t id, const SignatureCtx&)
    {
        return fnWalk({&db_.view_signatures_, id});
    });
}

void ViewVersions::walk_xrefs_from(HVersion_id_t version_id, const OnXrefFromFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id];
    ::walk_xrefs(db_, version, [&](const yadb::Xref* xref)
    {
        const auto ref = db_.get_object(xref->id());
        if(!ref.is_valid())
            return WALK_CONTINUE;
        return fnWalk(xref->offset(), xref->operand(), ref);
    });
}

void ViewVersions::walk_xrefs_to(HVersion_id_t version_id, const OnObjectFn& fnWalk) const
{
    const auto object_id = db_.versions_[version_id].object_id;
    db_.view_objects_.walk_xrefs_to(object_id, fnWalk);
}

void ViewVersions::walk_blobs(HVersion_id_t version_id, const OnBlobFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->blobs(), [&](const auto* blob)
    {
        const auto* data = blob->data();
        return fnWalk(blob->offset(), data->data(), (int) data->size());
    });
}

void ViewVersions::walk_comments(HVersion_id_t version_id, const OnCommentFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->comments(), [&](const auto* comment)
    {
        const auto val = comment->value();
        if(!val)
            return WALK_CONTINUE;
        return fnWalk(comment->offset(), get_comment_type(comment->type()), string_from(db_, val));
    });
}

void ViewVersions::walk_value_views(HVersion_id_t version_id, const OnValueViewFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->valueviews(), [&](const auto* view)
    {
        return fnWalk(view->offset(), view->operand(), string_from(db_, view->value()));
    });
}

void ViewVersions::walk_register_views(HVersion_id_t version_id, const OnRegisterViewFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->registerviews(), [&](const auto* view)
    {
        const auto name = view->register_name();
        const auto new_name = view->register_new_name();
        if(!name || !new_name)
            return WALK_CONTINUE;
        return fnWalk(view->offset(), view->end_offset(), string_from(db_, name), string_from(db_, new_name));
    });
}

void ViewVersions::walk_hidden_areas(HVersion_id_t version_id, const OnHiddenAreaFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->hiddenareas(), [&](const auto* area)
    {
        const auto val = area->value();
        if(!val)
            return WALK_CONTINUE;
        return fnWalk(area->offset(), area->area_size(), string_from(db_, val));
    });
}

void ViewVersions::walk_xrefs(HVersion_id_t version_id, const OnXrefFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id];
    ::walk_xrefs(db_, version, [&](const auto* xref)
    {
        return fnWalk(xref->offset(), xref->operand(), xref->id(), reinterpret_cast<const XrefAttributes*>(xref));
    });
}

void ViewVersions::walk_xref_attributes(HVersion_id_t, const XrefAttributes* hattr, const OnAttributeFn& fnWalk) const
{
    const auto* xref = reinterpret_cast<const yadb::Xref*>(hattr);
    walk_stoppable(xref->attributes(), [&](const auto* attr)
    {
        const auto key = attr->key();
        const auto val = attr->value();
        if(!key || !val)
            return WALK_CONTINUE;
        return fnWalk(string_from(db_, key), string_from(db_, val));
    });
}

void ViewVersions::walk_attributes(HVersion_id_t version_id, const OnAttributeFn& fnWalk) const
{
    const auto& version = db_.versions_[version_id].version;
    walk_stoppable(version->attributes(), [&](const auto* attr)
    {
        const auto key = attr->key();
        const auto val = attr->value();
        if(!key || !val)
            return WALK_CONTINUE;
        return fnWalk(string_from(db_, key), string_from(db_, val));
    });
}

Signature ViewSignatures::get(HSignature_id_t id) const
{
    const auto s = db_.signatures_[id].signature;
    return MakeSignature(get_signature_algo(s->type()), get_signature_method(s->method()), string_from(db_, s->value()));
}
