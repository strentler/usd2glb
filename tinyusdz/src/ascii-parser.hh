// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// USD ASCII parser

#pragma once

#include <cerrno>
#include <functional>
#include <stack>

//#include "external/better-enums/enum.h"
#include "prim-types.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external
#include "nonstd/expected.hpp"

//
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

namespace ascii {

// keywords
constexpr auto kUniform = "uniform";
constexpr auto kToken = "token";

// Frequently used attr/meta keywords
constexpr auto kKind = "kind";
constexpr auto kInterpolation = "interpolation";

struct Identifier : std::string {
  // using std::string;
};

// FIXME: Not used? remove.
struct PathIdentifier : std::string {
  // using std::string;
};

enum class LoadState {
  TOPLEVEL,   // toplevel .usda input
  SUBLAYER,   // .usda is read by 'subLayers'
  REFERENCE,  // .usda is read by `references`
  PAYLOAD,    // .usda is read by `payload`
};

// Prim Kind
// https://graphics.pixar.com/usd/release/glossary.html#usdglossary-kind
#if 1
enum class Kind {
  Model,         // "model"
  Group,         // "group"
  Assembly,      // "assembly"
  Component,     // "component"
  Subcomponent,  // "subcomponent"
};
#else

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

BETTER_ENUM(Kind, int, model, group, assembly, component, subcomponent);

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif

///
/// Test if input file is USDA ascii format.
///
bool IsUSDA(const std::string &filename, size_t max_filesize = 0);

class AsciiParser {
 public:
  // TODO: refactor
  struct PrimMetas {
    // Frequently used prim metas
    nonstd::optional<Kind> kind;

    value::dict customData;           // `customData`
    std::vector<StringData> strings;  // String only unregistered metadata.
  };

  // TODO: Unifity class with StageMetas in prim-types.hh
  struct StageMetas {
    ///
    /// Predefined Stage metas
    ///
    std::vector<value::AssetPath> subLayers;  // 'subLayers'
    value::token defaultPrim;                 // 'defaultPrim'
    StringData doc;                           // 'doc'
    nonstd::optional<Axis> upAxis;            // not specified = nullopt
    nonstd::optional<double> metersPerUnit;
    nonstd::optional<double> timeCodesPerSecond;
    nonstd::optional<double> startTimeCode;
    nonstd::optional<double> endTimeCode;
    nonstd::optional<double> framesPerSecond;

    std::map<std::string, MetaVariable> customLayerData;  // `customLayerData`.
    std::vector<StringData> strings;  // String only unregistered metadata.
  };

  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  struct Cursor {
    int row{0};
    int col{0};
  };

  struct ErrorDiagnositc {
    std::string err;
    Cursor cursor;
  };

  void PushError(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.cursor.row = _curr_cursor.row;
    diag.cursor.col = _curr_cursor.col;
    diag.err = msg;
    err_stack.push(diag);
  }

  // This function is used to cancel recent parsing error.
  void PopError() {
    if (!err_stack.empty()) {
      err_stack.pop();
    }
  }

  void PushWarn(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.cursor.row = _curr_cursor.row;
    diag.cursor.col = _curr_cursor.col;
    diag.err = msg;
    warn_stack.push(diag);
  }

  // This function is used to cancel recent parsing warning.
  void PopWarn() {
    if (!warn_stack.empty()) {
      warn_stack.pop();
    }
  }

  bool IsStageMeta(const std::string &name);
  bool IsPrimMeta(const std::string &name);

  class VariableDef {
   public:
    // Handler functor in post parsing stage.
    // e.g. Check input string is a valid one: one of "common", "group",
    // "assembly", "component" or "subcomponent" for "kind" metadata
    using PostParseHandler =
        std::function<nonstd::expected<bool, std::string>(const std::string &)>;

    static nonstd::expected<bool, std::string> DefaultPostParseHandler(
        const std::string &) {
      return true;
    }

    std::string type;  // e.g. token, color3f
    std::string name;
    bool allow_array_type{false};  // when true, we accept `type` and `type[]`

    PostParseHandler post_parse_handler;

    VariableDef() = default;

    VariableDef(const std::string &t, const std::string &n, bool a = false,
                PostParseHandler ph = DefaultPostParseHandler)
        : type(t), name(n), allow_array_type(a), post_parse_handler(ph) {}

    VariableDef(const VariableDef &rhs) = default;
    VariableDef &operator=(const VariableDef &rhs) = default;

    // VariableDef &operator=(const VariableDef &rhs) {
    //   type = rhs.type;
    //   name = rhs.name;
    //   parse_handler = rhs.parse_handler;

    //  return *this;
    //}
  };

  AsciiParser();
  AsciiParser(tinyusdz::StreamReader *sr);

  AsciiParser(const AsciiParser &rhs) = delete;
  AsciiParser(AsciiParser &&rhs) = delete;

  ~AsciiParser();

  ///
  /// Callback functions which is called from a class outside of
  /// AsciiParser(i.e. USDAReader)
  ///

  ///
  /// Assign index to primitive for index-based prim scene graph representation.
  /// -1 = root
  ///
  using PrimIdxAssignFunctin = std::function<int64_t(const int64_t parentIdx)>;
  void RegisterPrimIdxAssignFunction(PrimIdxAssignFunctin fun) {
    _prim_idx_assign_fun = fun;
  }

  ///
  /// Stage Meta construction callback function
  ///
  using StageMetaProcessFunction = std::function<bool(const StageMetas &metas)>;

  ///
  /// Register Stage metadatum processing callback function.
  /// Called when after parsing Stage metadatum.
  ///
  void RegisterStageMetaProcessFunction(StageMetaProcessFunction fun) {
    _stage_meta_process_fun = fun;
  }

  ///
  /// Prim Meta construction callback function
  ///
  // using PrimMetaProcessFunction = std::function<bool(const PrimMetas
  // &metas)>;

  using PrimMetaMap =
      std::map<std::string, std::pair<ListEditQual, MetaVariable>>;

  ///
  /// Prim construction callback function
  ///
  /// @param spec : Specifier(`def`, `over` or `class`)
  /// @param primIdx : primitive index
  /// @param parentPrimIdx : -1 for root
  /// @return true upon success or error message.
  ///
  using PrimConstructFunction =
      std::function<nonstd::expected<bool, std::string>(
          const Path &full_path, const Specifier spec, const Path &prim_name,
          const int64_t primIdx, const int64_t parentPrimIdx,
          const std::map<std::string, Property> &properties,
          const PrimMetaMap &in_meta)>;

  struct VariantContent {
    PrimMetaMap metas;
    std::vector<int64_t> primIndices;  // primIdx of Reconstrcuted Prim.
    std::map<std::string, Property> props;
  };

  ///
  /// Register Prim construction callback function.
  /// Example: "Xform", ReconstrctXform
  ///
  void RegisterPrimConstructFunction(const std::string &prim_type,
                                     PrimConstructFunction fun) {
    _prim_construct_fun_map[prim_type] = fun;
  }

  ///
  /// Callbacks called at closing `def` block.
  ///
  using PostPrimConstructFunction =
      std::function<nonstd::expected<bool, std::string>(
          const Path &path, const int64_t primIdx,
          const int64_t parentPrimIdx)>;
  void RegisterPostPrimConstructFunction(const std::string &prim_type,
                                         PostPrimConstructFunction fun) {
    _post_prim_construct_fun_map[prim_type] = fun;
  }

  ///
  /// Base filesystem directory to search asset files.
  ///
  void SetBaseDir(const std::string &base_dir);

  ///
  /// Set ASCII data stream
  ///
  void SetStream(tinyusdz::StreamReader *sr);

  ///
  /// Check if header data is USDA
  ///
  bool CheckHeader();

  ///
  /// Parser entry point
  ///
  bool Parse(LoadState state = LoadState::TOPLEVEL);


  ///
  /// Parse TimeSample value with specified `type_name`(Appears in USDA. .e.g. "float", "matrix2d")
  ///
  bool ParseTimeSampleValue(const std::string &type_name, value::Value *result);

  ///
  /// Parse TimeSample value with specified array type of `type_name`("[]" omiotted. .e.g. "float" for "float[]")
  ///
  bool ParseTimeSampleValueOfArrayType(const std::string &type_name, value::Value *result);

  // TODO: ParseBasicType?
  bool ParsePurpose(Purpose *result);

  ///
  /// Return true but `value` is set to nullopt for `None`(Attribute Blocked)
  ///
  // template <typename T>
  // bool ReadBasicType(nonstd::optional<T> *value);

  bool ReadBasicType(nonstd::optional<bool> *value);
  bool ReadBasicType(nonstd::optional<value::half> *value);
  bool ReadBasicType(nonstd::optional<value::half2> *value);
  bool ReadBasicType(nonstd::optional<value::half3> *value);
  bool ReadBasicType(nonstd::optional<value::half4> *value);
  bool ReadBasicType(nonstd::optional<int32_t> *value);
  bool ReadBasicType(nonstd::optional<value::int2> *value);
  bool ReadBasicType(nonstd::optional<value::int3> *value);
  bool ReadBasicType(nonstd::optional<value::int4> *value);
  bool ReadBasicType(nonstd::optional<uint32_t> *value);
  bool ReadBasicType(nonstd::optional<value::uint2> *value);
  bool ReadBasicType(nonstd::optional<value::uint3> *value);
  bool ReadBasicType(nonstd::optional<value::uint4> *value);
  bool ReadBasicType(nonstd::optional<int64_t> *value);
  bool ReadBasicType(nonstd::optional<uint64_t> *value);
  bool ReadBasicType(nonstd::optional<float> *value);
  bool ReadBasicType(nonstd::optional<value::float2> *value);
  bool ReadBasicType(nonstd::optional<value::float3> *value);
  bool ReadBasicType(nonstd::optional<value::float4> *value);
  bool ReadBasicType(nonstd::optional<double> *value);
  bool ReadBasicType(nonstd::optional<value::double2> *value);
  bool ReadBasicType(nonstd::optional<value::double3> *value);
  bool ReadBasicType(nonstd::optional<value::double4> *value);
  bool ReadBasicType(nonstd::optional<value::quath> *value);
  bool ReadBasicType(nonstd::optional<value::quatf> *value);
  bool ReadBasicType(nonstd::optional<value::quatd> *value);
  bool ReadBasicType(nonstd::optional<value::point3h> *value);
  bool ReadBasicType(nonstd::optional<value::point3f> *value);
  bool ReadBasicType(nonstd::optional<value::point3d> *value);
  bool ReadBasicType(nonstd::optional<value::vector3h> *value);
  bool ReadBasicType(nonstd::optional<value::vector3f> *value);
  bool ReadBasicType(nonstd::optional<value::vector3d> *value);
  bool ReadBasicType(nonstd::optional<value::normal3h> *value);
  bool ReadBasicType(nonstd::optional<value::normal3f> *value);
  bool ReadBasicType(nonstd::optional<value::normal3d> *value);
  bool ReadBasicType(nonstd::optional<value::color3h> *value);
  bool ReadBasicType(nonstd::optional<value::color3f> *value);
  bool ReadBasicType(nonstd::optional<value::color3d> *value);
  bool ReadBasicType(nonstd::optional<value::color4h> *value);
  bool ReadBasicType(nonstd::optional<value::color4f> *value);
  bool ReadBasicType(nonstd::optional<value::color4d> *value);
  bool ReadBasicType(nonstd::optional<value::matrix2d> *value);
  bool ReadBasicType(nonstd::optional<value::matrix3d> *value);
  bool ReadBasicType(nonstd::optional<value::matrix4d> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord2h> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord2f> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord2d> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord3h> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord3f> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord3d> *value);
  bool ReadBasicType(nonstd::optional<StringData> *value);
  bool ReadBasicType(nonstd::optional<std::string> *value);
  bool ReadBasicType(nonstd::optional<value::token> *value);
  bool ReadBasicType(nonstd::optional<Path> *value);
  bool ReadBasicType(nonstd::optional<value::AssetPath> *value);
  bool ReadBasicType(nonstd::optional<Reference> *value);
  bool ReadBasicType(nonstd::optional<Identifier> *value);
  bool ReadBasicType(nonstd::optional<PathIdentifier> *value);

  // template <typename T>
  // bool ReadBasicType(T *value);

  bool ReadBasicType(bool *value);
  bool ReadBasicType(value::half *value);
  bool ReadBasicType(value::half2 *value);
  bool ReadBasicType(value::half3 *value);
  bool ReadBasicType(value::half4 *value);
  bool ReadBasicType(int32_t *value);
  bool ReadBasicType(value::int2 *value);
  bool ReadBasicType(value::int3 *value);
  bool ReadBasicType(value::int4 *value);
  bool ReadBasicType(uint32_t *value);
  bool ReadBasicType(value::uint2 *value);
  bool ReadBasicType(value::uint3 *value);
  bool ReadBasicType(value::uint4 *value);
  bool ReadBasicType(int64_t *value);
  bool ReadBasicType(uint64_t *value);
  bool ReadBasicType(float *value);
  bool ReadBasicType(value::float2 *value);
  bool ReadBasicType(value::float3 *value);
  bool ReadBasicType(value::float4 *value);
  bool ReadBasicType(double *value);
  bool ReadBasicType(value::double2 *value);
  bool ReadBasicType(value::double3 *value);
  bool ReadBasicType(value::double4 *value);
  bool ReadBasicType(value::quath *value);
  bool ReadBasicType(value::quatf *value);
  bool ReadBasicType(value::quatd *value);
  bool ReadBasicType(value::point3h *value);
  bool ReadBasicType(value::point3f *value);
  bool ReadBasicType(value::point3d *value);
  bool ReadBasicType(value::vector3h *value);
  bool ReadBasicType(value::vector3f *value);
  bool ReadBasicType(value::vector3d *value);
  bool ReadBasicType(value::normal3h *value);
  bool ReadBasicType(value::normal3f *value);
  bool ReadBasicType(value::normal3d *value);
  bool ReadBasicType(value::color3h *value);
  bool ReadBasicType(value::color3f *value);
  bool ReadBasicType(value::color3d *value);
  bool ReadBasicType(value::color4h *value);
  bool ReadBasicType(value::color4f *value);
  bool ReadBasicType(value::color4d *value);
  bool ReadBasicType(value::texcoord2h *value);
  bool ReadBasicType(value::texcoord2f *value);
  bool ReadBasicType(value::texcoord2d *value);
  bool ReadBasicType(value::texcoord3h *value);
  bool ReadBasicType(value::texcoord3f *value);
  bool ReadBasicType(value::texcoord3d *value);
  bool ReadBasicType(value::matrix2d *value);
  bool ReadBasicType(value::matrix3d *value);
  bool ReadBasicType(value::matrix4d *value);
  bool ReadBasicType(StringData *value);
  bool ReadBasicType(std::string *value);
  bool ReadBasicType(value::token *value);
  bool ReadBasicType(Path *value);
  bool ReadBasicType(value::AssetPath *value);
  bool ReadBasicType(Reference *value);
  bool ReadBasicType(Identifier *value);
  bool ReadBasicType(PathIdentifier *value);

  template <typename T>
  bool ReadBasicType(nonstd::optional<std::vector<T>> *value);

  template <typename T>
  bool ReadBasicType(std::vector<T> *value);

  // Apprently, No float precision matrix in USDA
  bool ParseMatrix(value::matrix2d *result);
  bool ParseMatrix(value::matrix3d *result);
  bool ParseMatrix(value::matrix4d *result);

  ///
  /// Parse '(', Sep1By(','), ')'
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(std::array<T, N> *result);

  ///
  /// Parse '(', Sep1By(','), ')'
  /// Can have `None`
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(nonstd::optional<std::array<T, N>> *result);

  template <typename T, size_t N>
  bool ParseTupleArray(std::vector<std::array<T, N>> *result);

  ///
  /// Parse the array of tuple. some may be None(e.g. `float3`: [(0, 1, 2),
  /// None, (2, 3, 4), ...] )
  ///
  template <typename T, size_t N>
  bool ParseTupleArray(std::vector<nonstd::optional<std::array<T, N>>> *result);

  template <typename T>
  bool SepBy1BasicType(const char sep, std::vector<T> *result);

  ///
  /// Allow the appearance of `sep` in the last item of array.
  /// (e.g. `[1, 2, 3,]`)
  ///
  template <typename T>
  bool SepBy1BasicType(const char sep, const char end_symbol,
                       std::vector<T> *result);

  ///
  /// Parse '[', Sep1By(','), ']'
  ///
  template <typename T>
  bool ParseBasicTypeArray(std::vector<nonstd::optional<T>> *result);

  ///
  /// Parse '[', Sep1By(','), ']'
  ///
  template <typename T>
  bool ParseBasicTypeArray(std::vector<T> *result);

  ///
  /// Parses 1 or more occurences of value with basic type 'T', separated by
  /// `sep`
  ///
  template <typename T>
  bool SepBy1BasicType(const char sep,
                       std::vector<nonstd::optional<T>> *result);

  ///
  /// Parses 1 or more occurences of tuple values with type 'T', separated by
  /// `sep`. Allows 'None'
  ///
  template <typename T, size_t N>
  bool SepBy1TupleType(const char sep,
                       std::vector<nonstd::optional<std::array<T, N>>> *result);

  ///
  /// Parses 1 or more occurences of tuple values with type 'T', separated by
  /// `sep`
  ///
  template <typename T, size_t N>
  bool SepBy1TupleType(const char sep, std::vector<std::array<T, N>> *result);

  bool ParseDictElement(std::string *out_key, MetaVariable *out_var);
  bool ParseDict(std::map<std::string, MetaVariable> *out_dict);

  ///
  /// Parse TimeSample data(scalar type) and store it to type-erased data
  /// structure value::TimeSamples.
  ///
  /// @param[in] type_name Name of TimeSamples type(seen in .usda file. e.g.
  /// "float" for `float var.timeSamples = ..`)
  ///
  bool ParseTimeSamples(const std::string &type_name, value::TimeSamples *ts);

  ///
  /// Parse TimeSample data(array type) and store it to type-erased data
  /// structure value::TimeSamples.
  ///
  /// @param[in] type_name Name of TimeSamples type(seen in .usda file. array
  /// suffix `[]` is omitted. e.g. "float" for `float[] var.timeSamples = ..`)
  ///
  bool ParseTimeSamplesOfArray(const std::string &type_name,
                               value::TimeSamples *ts);

  ///
  /// `variants` in Prim meta.
  ///
  bool ParseVariantsElement(std::string *out_key, std::string *out_var);
  bool ParseVariants(VariantSelectionMap *out_map);

  bool MaybeListEditQual(tinyusdz::ListEditQual *qual);

  ///
  /// Try parsing single-quoted(`"`) string
  ///
  bool MaybeString(StringData *str);

  ///
  /// Try parsing triple-quited(`"""`) multi-line string.
  ///
  bool MaybeTripleQuotedString(StringData *str);

  ///
  /// Parse assset path identifier.
  ///
  bool ParseAssetIdentifier(value::AssetPath *out, bool *triple_deliminated);

#if 0
  ///
  ///
  ///
  std::string GetDefaultPrimName() const;

  ///
  /// Get parsed toplevel "def" nodes(GPrim)
  ///
  std::vector<GPrim> GetGPrims();
#endif
  class PrimIterator;
  using const_iterator = PrimIterator;
  const_iterator begin() const;
  const_iterator end() const;

  ///
  /// Get error message(when `Parse` failed)
  ///
  std::string GetError();

  ///
  /// Get warning message(warnings in `Parse`)
  ///
  std::string GetWarning();

  // Return the flag if the .usda is read from `references`
  bool IsReferenced() { return _referenced; }

  // Return the flag if the .usda is read from `subLayers`
  bool IsSubLayered() { return _sub_layered; }

  // Return the flag if the .usda is read from `payload`
  bool IsPayloaded() { return _payloaded; }

  // Return true if the .udsa is read in the top layer(stage)
  bool IsToplevel() {
    return !IsReferenced() && !IsSubLayered() && !IsPayloaded();
  }

  bool MaybeNone();
  bool MaybeCustom();

  template <typename T>
  bool MaybeNonFinite(T *out);

  bool LexFloat(std::string *result);

  bool Expect(char expect_c);

  bool ReadStringLiteral(
      std::string *literal);  // identifier wrapped with " or '. result contains
                              // quote chars.
  bool ReadPrimAttrIdentifier(std::string *token);
  bool ReadIdentifier(std::string *token);  // no '"'
  bool ReadPathIdentifier(
      std::string *path_identifier);  // '<' + identifier + '>'

  /// Parse magic
  /// #usda FLOAT
  bool ParseMagicHeader();

  bool SkipWhitespace();

  // skip_semicolon true: ';' can be used as a separator. this flag is for
  // statement block.
  bool SkipWhitespaceAndNewline(bool allow_semicolon = true);

  bool SkipCommentAndWhitespaceAndNewline();
  bool SkipUntilNewline();

  // bool ParseAttributeMeta();
  bool ParseAttrMeta(AttrMeta *out_meta);

  bool ParsePrimMetas(PrimMetaMap *out_metamap);

  bool ParseMetaValue(const VariableDef &def, MetaVariable *outvar);

  bool ParseStageMetaOpt();
  // Parsed Stage metadatum is stored in this instance.
  bool ParseStageMetas();

  bool ParseCustomMetaValue();

  // TODO: Return Path?
  bool ParseReference(Reference *out, bool *triple_deliminated);

  // `#` style comment
  bool ParseSharpComment();

  bool IsSupportedPrimAttrType(const std::string &ty);
  bool IsSupportedPrimType(const std::string &ty);
  bool IsSupportedAPISchema(const std::string &ty);

  bool Eof() { return _sr->eof(); }

  bool ParseRelation(Relation *result);
  bool ParseProperty(std::map<std::string, Property> *props);

  //
  // Look***() : Fetch chars but do not change input stream position.
  //

  bool LookChar1(char *c);
  bool LookCharN(size_t n, std::vector<char> *nc);

  bool Char1(char *c);
  bool CharN(size_t n, std::vector<char> *nc);

  bool Rewind(size_t offset);
  uint64_t CurrLoc();
  bool SeekTo(uint64_t pos);  // Move to absolute `pos` bytes location

  bool PushParserState();
  bool PopParserState(ParseState *state);

  //
  // Valid after ParseStageMetas() --------------
  //
  StageMetas GetStageMetas() const { return _stage_metas; }

  // primIdx is assigned through `PrimIdxAssignFunctin`
  // parentPrimIdx = -1 => root prim
  // depth = tree level(recursion count)
  // bool ParseClassBlock(const int64_t primIdx, const int64_t parentPrimIdx,
  // const uint32_t depth = 0); bool ParseOverBlock(const int64_t primIdx, const
  // int64_t parentPrimIdx, const uint32_t depth = 0); bool ParseDefBlock(const
  // int64_t primIdx, const int64_t parentPrimIdx, const uint32_t depth = 0);

  // Parse `def`, `over` or `class` block
  // @param[in] in_variantStmt : true when this Block is parsed within
  // `variantSet` statement. Default true.
  bool ParseBlock(const Specifier spec, const int64_t primIdx,
                  const int64_t parentPrimIdx, const uint32_t depth,
                  const bool in_variant = false);

  // Parse `varianntSet` stmt
  bool ParseVariantSet(const int64_t primIdx, const int64_t parentPrimIdx,
                       const uint32_t depth = 0);

  // --------------------------------------------

 private:
  ///
  /// Do common setups. Assume called in ctor.
  ///
  void Setup();

  // template<typename T>
  // bool ParseTimeSampleData(nonstd::optional<T> *out_value);

  // -- [TimeSamples] -------------------

  //template <typename T>
  //using TimeSampleData = std::vector<std::pair<double, nonstd::optional<T>>>;

  // template <typename T>
  // using TimeSampleDataArray = std::vector<std::pair<double,
  // nonstd::optional<std::vector<T>>>>;

  ///
  /// Convert TimeSampleData<T> to TimeSamples(type-erased TimeSample Sdata
  /// struct)
  ///
  //template <typename T>
  //value::TimeSamples ConvertToTimeSamples(const TimeSampleData<T> &in);

  //template <typename T>
  //value::TimeSamples ConvertToTimeSamples(
  //    const TimeSampleData<std::vector<T>> &in);

  //// T = scalar(e.g. `float`)
  //template <typename T>
  //nonstd::optional<TimeSampleData<T>> TryParseTimeSamples();

  //template <typename T>
  //nonstd::optional<TimeSampleData<std::vector<T>>> TryParseTimeSamplesOfArray();

  // ---------------------------------------

  nonstd::optional<std::pair<ListEditQual, MetaVariable>> ParsePrimMeta();
  bool ParsePrimProps(std::map<std::string, Property> *props);

  template <typename T>
  bool ParseBasicPrimAttr(bool array_qual, const std::string &primattr_name,
                          PrimAttrib *out_attr);

  bool ParseStageMeta(std::pair<ListEditQual, MetaVariable> *out);

  nonstd::optional<VariableDef> GetStageMetaDefinition(const std::string &name);
  nonstd::optional<VariableDef> GetPrimMetaDefinition(const std::string &arg);
  nonstd::optional<VariableDef> GetPropMetaDefinition(const std::string &arg);

  std::string GetCurrentPath();
  bool PathStackDepth() { return _path_stack.size(); }
  void PushPath(const std::string &p) { _path_stack.push(p); }
  void PopPath() {
    if (!_path_stack.empty()) {
      _path_stack.pop();
    }
  }

  const tinyusdz::StreamReader *_sr = nullptr;

  // "class" defs
  // std::map<std::string, Klass> _klasses;
  std::stack<std::string> _path_stack;

#if 0
  // Cache of loaded `references`
  // <filename, {defaultPrim index, list of root nodes in referenced usd file}>
  std::map<std::string, std::pair<uint32_t, std::vector<GPrim>>>
        _reference_cache;

    // toplevel "def" defs
    std::vector<GPrim> _gprims;
#endif

  Cursor _curr_cursor;

  // Supported Prim types
  std::set<std::string> _supported_prim_types;
  std::set<std::string> _supported_prim_attr_types;

  // Supported API schemas
  std::set<std::string> _supported_api_schemas;

  // Supported metadataum for Stage
  std::map<std::string, VariableDef> _supported_stage_metas;

  // Supported metadataum for Prim.
  std::map<std::string, VariableDef> _supported_prim_metas;

  // Supported metadataum for Property(Attribute and Relation).
  std::map<std::string, VariableDef> _supported_prop_metas;

  std::stack<ErrorDiagnositc> err_stack;
  std::stack<ErrorDiagnositc> warn_stack;
  std::stack<ParseState> parse_stack;

  float _version{1.0f};

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _base_dir;

  StageMetas _stage_metas;

  //
  // Callbacks
  //
  PrimIdxAssignFunctin _prim_idx_assign_fun;
  StageMetaProcessFunction _stage_meta_process_fun;
  // PrimMetaProcessFunction _prim_meta_process_fun;
  std::map<std::string, PrimConstructFunction> _prim_construct_fun_map;
  std::map<std::string, PostPrimConstructFunction> _post_prim_construct_fun_map;

  // class Impl;
  // Impl *_impl;
};

}  // namespace ascii

}  // namespace tinyusdz