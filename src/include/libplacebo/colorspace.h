/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBPLACEBO_COLORSPACE_H_
#define LIBPLACEBO_COLORSPACE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libplacebo/common.h>

PL_API_BEGIN

// The underlying color representation (e.g. RGB, XYZ or YCbCr)
enum pl_color_system {
    PL_COLOR_SYSTEM_UNKNOWN = 0,
    // YCbCr-like color systems:
    PL_COLOR_SYSTEM_BT_601,      // ITU-R Rec. BT.601 (SD)
    PL_COLOR_SYSTEM_BT_709,      // ITU-R Rec. BT.709 (HD)
    PL_COLOR_SYSTEM_SMPTE_240M,  // SMPTE-240M
    PL_COLOR_SYSTEM_BT_2020_NC,  // ITU-R Rec. BT.2020 (non-constant luminance)
    PL_COLOR_SYSTEM_BT_2020_C,   // ITU-R Rec. BT.2020 (constant luminance)
    PL_COLOR_SYSTEM_BT_2100_PQ,  // ITU-R Rec. BT.2100 ICtCp PQ variant
    PL_COLOR_SYSTEM_BT_2100_HLG, // ITU-R Rec. BT.2100 ICtCp HLG variant
    PL_COLOR_SYSTEM_DOLBYVISION, // Dolby Vision (see pl_dovi_metadata)
    PL_COLOR_SYSTEM_YCGCO,       // YCgCo (derived from RGB)
    // Other color systems:
    PL_COLOR_SYSTEM_RGB,         // Red, Green and Blue
    PL_COLOR_SYSTEM_XYZ,         // CIE 1931 XYZ, pre-encoded with gamma 2.6
    PL_COLOR_SYSTEM_COUNT
};

bool pl_color_system_is_ycbcr_like(enum pl_color_system sys);

// Returns true for color systems that are linear transformations of the RGB
// equivalent, i.e. are simple matrix multiplications. For color systems with
// this property, `pl_color_repr_decode` is sufficient for conversion to RGB.
bool pl_color_system_is_linear(enum pl_color_system sys);

// Guesses the best YCbCr-like colorspace based on a image given resolution.
// This only picks conservative values. (In particular, BT.2020 is never
// auto-guessed, even for 4K resolution content)
enum pl_color_system pl_color_system_guess_ycbcr(int width, int height);

// Friendly names for the canonical channel names and order.
enum pl_channel {
    PL_CHANNEL_NONE = -1,
    PL_CHANNEL_A = 3, // alpha
    // RGB system
    PL_CHANNEL_R = 0,
    PL_CHANNEL_G = 1,
    PL_CHANNEL_B = 2,
    // YCbCr-like systems
    PL_CHANNEL_Y = 0,
    PL_CHANNEL_CB = 1,
    PL_CHANNEL_CR = 2,
    // Aliases for Cb/Cr
    PL_CHANNEL_U = 1,
    PL_CHANNEL_V = 2
    // There are deliberately no names for the XYZ system to avoid
    // confusion due to PL_CHANNEL_Y.
};

// The numerical range of the representation (where applicable).
enum pl_color_levels {
    PL_COLOR_LEVELS_UNKNOWN = 0,
    PL_COLOR_LEVELS_LIMITED,    // Limited/TV range, e.g. 16-235
    PL_COLOR_LEVELS_FULL,       // Full/PC range, e.g. 0-255
    PL_COLOR_LEVELS_COUNT,

    // Compatibility aliases
    PL_COLOR_LEVELS_TV = PL_COLOR_LEVELS_LIMITED,
    PL_COLOR_LEVELS_PC = PL_COLOR_LEVELS_FULL,
};

// The alpha representation mode.
enum pl_alpha_mode {
    PL_ALPHA_UNKNOWN = 0,   // or no alpha channel present
    PL_ALPHA_INDEPENDENT,   // alpha channel is separate from the video
    PL_ALPHA_PREMULTIPLIED, // alpha channel is multiplied into the colors
    PL_ALPHA_MODE_COUNT,
};

// The underlying bit-wise representation of a color sample. For example,
// a 10-bit TV-range YCbCr value uploaded to a 16 bit texture would have
// sample_depth=16 color_depth=10 bit_shift=0.
//
// For another example, a 12-bit XYZ full range sample shifted to 16-bits with
// the lower 4 bits all set to 0 would have sample_depth=16 color_depth=12
// bit_shift=4. (libavcodec likes outputting this type of `xyz12`)
//
// To explain the meaning of `sample_depth` further; the consideration factor
// here is the fact that GPU sampling will normalized the sampled color to the
// range 0.0 - 1.0 in a manner dependent on the number of bits in the texture
// format. So if you upload a 10-bit YCbCr value unpadded as 16-bit color
// samples, all of the sampled values will be extremely close to 0.0. In such a
// case, `pl_color_repr_normalize` would return a high scaling factor, which
// would pull the color up to their 16-bit range.
struct pl_bit_encoding {
    int sample_depth; // the number of bits the color is stored/sampled as
    int color_depth;  // the effective number of bits of the color information
    int bit_shift;    // a representational bit shift applied to the color
};

// Returns whether two bit encodings are exactly identical.
bool pl_bit_encoding_equal(const struct pl_bit_encoding *b1,
                           const struct pl_bit_encoding *b2);

// Parsed metadata from the Dolby Vision RPU
struct pl_dovi_metadata {
    // Colorspace transformation metadata
    float nonlinear_offset[3];      // input offset ("ycc_to_rgb_offset")
    struct pl_matrix3x3 nonlinear;  // before PQ, also called "ycc_to_rgb"
    struct pl_matrix3x3 linear;     // after PQ, also called "rgb_to_lms"

    // Reshape data, grouped by component
    struct pl_reshape_data {
        uint8_t num_pivots;
        float pivots[9]; // normalized to [0.0, 1.0] based on BL bit depth
        uint8_t method[8]; // 0 = polynomial, 1 = MMR
        // Note: these must be normalized (divide by coefficient_log2_denom)
        float poly_coeffs[8][3]; // x^0, x^1, x^2, unused must be 0
        uint8_t mmr_order[8]; // 1, 2 or 3
        float mmr_constant[8];
        float mmr_coeffs[8][3 /* order */][7];
    } comp[3];
};

// Struct describing the underlying color system and representation. This
// information is needed to convert an encoded color to a normalized RGB triple
// in the range 0-1.
struct pl_color_repr {
    enum pl_color_system sys;
    enum pl_color_levels levels;
    enum pl_alpha_mode alpha;
    struct pl_bit_encoding bits; // or {0} if unknown

    // Metadata for PL_COLOR_SYSTEM_DOLBYVISION. Note that, for the sake of
    // efficiency, this is treated purely as an opaque reference - functions
    // like pl_color_repr_equal will merely do a pointer equality test.
    //
    // The only functions that actually dereference it in any way are
    // pl_color_repr_decode,  pl_shader_decode_color and pl_render_image(_mix).
    const struct pl_dovi_metadata *dovi;
};

// Some common color representations. It's worth pointing out that all of these
// presets leave `alpha` and `bits` as unknown - that is, only the system and
// levels are predefined
extern const struct pl_color_repr pl_color_repr_unknown;
extern const struct pl_color_repr pl_color_repr_rgb;
extern const struct pl_color_repr pl_color_repr_sdtv;
extern const struct pl_color_repr pl_color_repr_hdtv;  // also Blu-ray
extern const struct pl_color_repr pl_color_repr_uhdtv; // SDR, NCL system
extern const struct pl_color_repr pl_color_repr_jpeg;

// Returns whether two colorspace representations are exactly identical.
bool pl_color_repr_equal(const struct pl_color_repr *c1,
                         const struct pl_color_repr *c2);

// Replaces unknown values in the first struct by those of the second struct.
void pl_color_repr_merge(struct pl_color_repr *orig,
                         const struct pl_color_repr *update);

// This function normalizes the color representation such that
// color_depth=sample_depth and bit_shift=0; and returns the scaling factor
// that must be multiplied into the color value to accomplish this, assuming
// it has already been sampled by the GPU. If unknown, the color and sample
// depth will both be inferred as 8 bits for the purposes of this conversion.
float pl_color_repr_normalize(struct pl_color_repr *repr);

// Guesses the best color levels based on the specified color levels and
// falling back to using the color system instead. YCbCr-like systems are
// assumed to be TV range, otherwise this defaults to PC range.
enum pl_color_levels pl_color_levels_guess(const struct pl_color_repr *repr);

// The colorspace's primaries (gamut)
enum pl_color_primaries {
    PL_COLOR_PRIM_UNKNOWN = 0,
    // Standard gamut:
    PL_COLOR_PRIM_BT_601_525,   // ITU-R Rec. BT.601 (525-line = NTSC, SMPTE-C)
    PL_COLOR_PRIM_BT_601_625,   // ITU-R Rec. BT.601 (625-line = PAL, SECAM)
    PL_COLOR_PRIM_BT_709,       // ITU-R Rec. BT.709 (HD), also sRGB
    PL_COLOR_PRIM_BT_470M,      // ITU-R Rec. BT.470 M
    PL_COLOR_PRIM_EBU_3213,     // EBU Tech. 3213-E / JEDEC P22 phosphors
    // Wide gamut:
    PL_COLOR_PRIM_BT_2020,      // ITU-R Rec. BT.2020 (UltraHD)
    PL_COLOR_PRIM_APPLE,        // Apple RGB
    PL_COLOR_PRIM_ADOBE,        // Adobe RGB (1998)
    PL_COLOR_PRIM_PRO_PHOTO,    // ProPhoto RGB (ROMM)
    PL_COLOR_PRIM_CIE_1931,     // CIE 1931 RGB primaries
    PL_COLOR_PRIM_DCI_P3,       // DCI-P3 (Digital Cinema)
    PL_COLOR_PRIM_DISPLAY_P3,   // DCI-P3 (Digital Cinema) with D65 white point
    PL_COLOR_PRIM_V_GAMUT,      // Panasonic V-Gamut (VARICAM)
    PL_COLOR_PRIM_S_GAMUT,      // Sony S-Gamut
    PL_COLOR_PRIM_FILM_C,       // Traditional film primaries with Illuminant C
    PL_COLOR_PRIM_COUNT
};

bool pl_color_primaries_is_wide_gamut(enum pl_color_primaries prim);

// Guesses the best primaries based on a resolution. This always guesses
// conservatively, i.e. it will never return a wide gamut color space even if
// the resolution is 4K.
enum pl_color_primaries pl_color_primaries_guess(int width, int height);

// The colorspace's transfer function (gamma / EOTF)
enum pl_color_transfer {
    PL_COLOR_TRC_UNKNOWN = 0,
    // Standard dynamic range:
    PL_COLOR_TRC_BT_1886,       // ITU-R Rec. BT.1886 (CRT emulation + OOTF)
    PL_COLOR_TRC_SRGB,          // IEC 61966-2-4 sRGB (CRT emulation)
    PL_COLOR_TRC_LINEAR,        // Linear light content
    PL_COLOR_TRC_GAMMA18,       // Pure power gamma 1.8
    PL_COLOR_TRC_GAMMA20,       // Pure power gamma 2.0
    PL_COLOR_TRC_GAMMA22,       // Pure power gamma 2.2
    PL_COLOR_TRC_GAMMA24,       // Pure power gamma 2.4
    PL_COLOR_TRC_GAMMA26,       // Pure power gamma 2.6
    PL_COLOR_TRC_GAMMA28,       // Pure power gamma 2.8
    PL_COLOR_TRC_PRO_PHOTO,     // ProPhoto RGB (ROMM)
    // High dynamic range:
    PL_COLOR_TRC_PQ,            // ITU-R BT.2100 PQ (perceptual quantizer), aka SMPTE ST2048
    PL_COLOR_TRC_HLG,           // ITU-R BT.2100 HLG (hybrid log-gamma), aka ARIB STD-B67
    PL_COLOR_TRC_V_LOG,         // Panasonic V-Log (VARICAM)
    PL_COLOR_TRC_S_LOG1,        // Sony S-Log1
    PL_COLOR_TRC_S_LOG2,        // Sony S-Log2
    PL_COLOR_TRC_COUNT
};

// Returns the nominal peak of a given transfer function, relative to the
// reference white. This refers to the highest encodable signal level.
// Always equal to 1.0 for SDR curves.
//
// Note: This returns the highest encodable signal by definition of the EOTF,
// regardless of the ultimate representation (e.g. scene or display referred).
// For HLG in particular, this is always around 3.77 - which is potentially
// different from the signal peak after applying the OOTF to go from scene
// referred to display referred (resulting in a display-referred peak of around
// 4.92 for a 1000 cd/m^2 HLG reference display).
float pl_color_transfer_nominal_peak(enum pl_color_transfer trc);

static inline bool pl_color_transfer_is_hdr(enum pl_color_transfer trc)
{
    return pl_color_transfer_nominal_peak(trc) > 1.0;
}

// This defines the display-space standard reference white level (in cd/m^2)
// that is assumed for SDR content, for use when mapping between HDR and SDR in
// display space. See ITU-R Report BT.2408 for more information.
#define PL_COLOR_SDR_WHITE 203.0f

// Deprecated. For compatibility with older versions of libplacebo.
#define PL_COLOR_REF_WHITE      PL_COLOR_SDR_WHITE
#define PL_COLOR_SDR_WHITE_HLG  3.17955

// Represents a single CIE xy coordinate (e.g. CIE Yxy with Y = 1.0)
struct pl_cie_xy {
    float x, y;
};

// Creates a pl_cie_xyz from raw XYZ values
static inline struct pl_cie_xy pl_cie_from_XYZ(float X, float Y, float Z)
{
    float k = 1.0f / (X + Y + Z);
    return (struct pl_cie_xy) { k * X, k * Y };
}

// Recovers (X / Y) from a CIE xy value.
static inline float pl_cie_X(struct pl_cie_xy xy)
{
    return xy.x / xy.y;
}

// Recovers (Z / Y) from a CIE xy value.
static inline float pl_cie_Z(struct pl_cie_xy xy)
{
    return (1 - xy.x - xy.y) / xy.y;
}

static inline bool pl_cie_xy_equal(const struct pl_cie_xy *a,
                                   const struct pl_cie_xy *b)
{
    return a->x == b->x && a->y == b->y;
}

// Computes the CIE xy chromaticity coordinates of a CIE D-series illuminant
// with the given correlated color temperature.
//
// `temperature` must be between 2500 K and 25000 K, inclusive.
struct pl_cie_xy pl_white_from_temp(float temperature);

// Represents the raw physical primaries corresponding to a color space.
struct pl_raw_primaries {
    struct pl_cie_xy red, green, blue, white;
};

// Returns whether two raw primaries are exactly identical.
bool pl_raw_primaries_equal(const struct pl_raw_primaries *a,
                            const struct pl_raw_primaries *b);

// Returns whether two raw primaries are approximately equal
bool pl_raw_primaries_similar(const struct pl_raw_primaries *a,
                              const struct pl_raw_primaries *b);

// Replaces unknown values in the first struct by those of the second struct.
void pl_raw_primaries_merge(struct pl_raw_primaries *orig,
                            const struct pl_raw_primaries *update);

// Returns the raw primaries for a given color space.
const struct pl_raw_primaries *pl_raw_primaries_get(enum pl_color_primaries prim);

// Represents raw HDR metadata as defined by SMPTE 2086 / CTA 861.3, which is
// often attached to HDR sources and can be forwarded to HDR-capable displays,
// or used to guide the libplacebo built-in tone mapping.
struct pl_hdr_metadata {
    // Mastering display metadata. This is used for tone-mapping.
    struct pl_raw_primaries prim; // mastering display primaries
    float min_luma, max_luma;     // min/max luminance (in cd/m²)

    // Content light level. This is ignored by libplacebo itself.
    float max_cll;                // max content light level (in cd/m²)
    float max_fall;               // max frame average light level (in cd/m²)
};

extern const struct pl_hdr_metadata pl_hdr_metadata_empty; // equal to {0}
extern const struct pl_hdr_metadata pl_hdr_metadata_hdr10; // generic HDR10 display

// Returns whether two sets of HDR metadata are exactly identical.
bool pl_hdr_metadata_equal(const struct pl_hdr_metadata *a,
                           const struct pl_hdr_metadata *b);

// Replaces unknown values in the first struct by those of the second struct.
void pl_hdr_metadata_merge(struct pl_hdr_metadata *orig,
                           const struct pl_hdr_metadata *update);

// Deprecated. No longer used by libplacebo.
enum pl_color_light {
    PL_COLOR_LIGHT_UNKNOWN = 0,
    PL_COLOR_LIGHT_DISPLAY,     // Display-referred, output as-is
    PL_COLOR_LIGHT_SCENE_HLG,   // Scene-referred, HLG OOTF
    PL_COLOR_LIGHT_SCENE_709_1886, // Scene-referred, OOTF = BT.709+1886 interaction
    PL_COLOR_LIGHT_SCENE_1_2,   // Scene-referred, OOTF = gamma 1.2
    PL_COLOR_LIGHT_COUNT
};

PL_DEPRECATED bool pl_color_light_is_scene_referred(enum pl_color_light light);

// Rendering intent for colorspace transformations. These constants match the
// ICC specification (Table 23)
enum pl_rendering_intent {
    PL_INTENT_AUTO = -1, // not a valid ICC intent, but used to auto-infer
    PL_INTENT_PERCEPTUAL = 0,
    PL_INTENT_RELATIVE_COLORIMETRIC = 1,
    PL_INTENT_SATURATION = 2,
    PL_INTENT_ABSOLUTE_COLORIMETRIC = 3
};

// Struct describing a physical color space. This information is needed to
// turn a normalized RGB triple into its physical meaning, as well as to convert
// between color spaces.
struct pl_color_space {
    enum pl_color_primaries primaries;
    enum pl_color_transfer transfer;

    // HDR metadata for this color space. Note that this can also be combined
    // with SDR color transfers, in which case it's assumed that the color
    // transfer in question is linearly "stretched" relative to these values.
    struct pl_hdr_metadata hdr;

    // Deprecated fields
    enum pl_color_light light PL_DEPRECATED;    // ignored
    float sig_peak PL_DEPRECATED;               // replaced by `hdr.max_luma`
    float sig_avg PL_DEPRECATED;                // ignored
    float sig_floor PL_DEPRECATED;              // replaced by `hdr.min_luma`
    float sig_scale PL_DEPRECATED;              // merged into `hdr.max/min_luma`
};

#define pl_color_space(...) (&(struct pl_color_space) { __VA_ARGS__ })

// Returns whether or not a color space is considered as effectively HDR.
// This is true when the effective signal peak is greater than the SDR
// reference white (1.0), taking into account `csp->hdr`.
bool pl_color_space_is_hdr(const struct pl_color_space *csp);

// Returns whether or not a color space is "black scaled", in which case 0.0 is
// the true black point. This is true for SDR signals other than BT.1886, as
// well as for HLG.
bool pl_color_space_is_black_scaled(const struct pl_color_space *csp);

// Replaces unknown values in the first struct by those of the second struct.
void pl_color_space_merge(struct pl_color_space *orig,
                          const struct pl_color_space *update);

// Returns whether two colorspaces are exactly identical.
bool pl_color_space_equal(const struct pl_color_space *c1,
                          const struct pl_color_space *c2);

// Go through a color-space and explicitly default all unknown fields to
// reasonable values. After this function is called, none of the values will be
// PL_COLOR_*_UNKNOWN or 0.0.
void pl_color_space_infer(struct pl_color_space *space);

// Like `pl_color_space_infer`, but takes default values from the reference
// color space (excluding certain special cases like HDR or wide gamut). This
// is basically the logic used by `pl_shader_color_map` to decide the output
// color space in a conservative way.
void pl_color_space_infer_ref(struct pl_color_space *space,
                              const struct pl_color_space *ref);

// Some common color spaces. Note: These don't necessarily have all fields
// filled, in particular `hdr` is left unset.
extern const struct pl_color_space pl_color_space_unknown;
extern const struct pl_color_space pl_color_space_srgb;
extern const struct pl_color_space pl_color_space_bt709;
extern const struct pl_color_space pl_color_space_hdr10;
extern const struct pl_color_space pl_color_space_bt2020_hlg;
extern const struct pl_color_space pl_color_space_monitor; // typical display

// This represents metadata about extra operations to perform during colorspace
// conversion, which correspond to artistic adjustments of the color.
struct pl_color_adjustment {
    // Brightness boost. 0.0 = neutral, 1.0 = solid white, -1.0 = solid black
    float brightness;
    // Contrast boost. 1.0 = neutral, 0.0 = solid black
    float contrast;
    // Saturation gain. 1.0 = neutral, 0.0 = grayscale
    float saturation;
    // Hue shift, corresponding to a rotation around the [U, V] subvector, in
    // radians. Only meaningful for YCbCr-like colorspaces. 0.0 = neutral
    float hue;
    // Gamma adjustment. 1.0 = neutral, 0.0 = solid black
    float gamma;
    // Color temperature shift. 0.0 = 6500 K, -1.0 = 3000 K, 1.0 = 10000 K
    float temperature;
};

// A struct pre-filled with all-neutral values.
extern const struct pl_color_adjustment pl_color_adjustment_neutral;

// Represents the chroma placement with respect to the luma samples. This is
// only relevant for YCbCr-like colorspaces with chroma subsampling.
enum pl_chroma_location {
    PL_CHROMA_UNKNOWN = 0,
    PL_CHROMA_LEFT,             // MPEG2/4, H.264
    PL_CHROMA_CENTER,           // MPEG1, JPEG
    PL_CHROMA_TOP_LEFT,
    PL_CHROMA_TOP_CENTER,
    PL_CHROMA_BOTTOM_LEFT,
    PL_CHROMA_BOTTOM_CENTER,
    PL_CHROMA_COUNT,
};

// Fills *x and *y with the offset in luma pixels corresponding to a given
// chroma location.
//
// Note: PL_CHROMA_UNKNOWN defaults to PL_CHROMA_LEFT
void pl_chroma_location_offset(enum pl_chroma_location loc, float *x, float *y);

// Returns an RGB->XYZ conversion matrix for a given set of primaries.
// Multiplying this into the RGB color transforms it to CIE XYZ, centered
// around the color space's white point.
struct pl_matrix3x3 pl_get_rgb2xyz_matrix(const struct pl_raw_primaries *prim);

// Similar to pl_get_rgb2xyz_matrix, but gives the inverse transformation.
struct pl_matrix3x3 pl_get_xyz2rgb_matrix(const struct pl_raw_primaries *prim);

// Returns a primary adaptation matrix, which converts from one set of
// primaries to another. This is an RGB->RGB transformation. For rendering
// intents other than PL_INTENT_ABSOLUTE_COLORIMETRIC, the white point is
// adapted using the Bradford matrix.
struct pl_matrix3x3 pl_get_color_mapping_matrix(const struct pl_raw_primaries *src,
                                                const struct pl_raw_primaries *dst,
                                                enum pl_rendering_intent intent);

// Return a chromatic adaptation matrix, which converts from one white point to
// another, using the Bradford matrix. This is an RGB->RGB transformation.
struct pl_matrix3x3 pl_get_adaptation_matrix(struct pl_cie_xy src, struct pl_cie_xy dst);

// Returns true if 'b' is entirely contained in 'a'. Useful for figuring out if
// colorimetric clipping will occur or not.
bool pl_primaries_superset(const struct pl_raw_primaries *a,
                           const struct pl_raw_primaries *b);

// Returns true if `prim` forms a nominally valid set of primaries. This does
// not check whether or not these primaries are actually physically realisable,
// merely that they satisfy the requirements for colorspace math (to avoid NaN).
bool pl_primaries_valid(const struct pl_raw_primaries *prim);

// Cone types involved in human vision
enum pl_cone {
    PL_CONE_L = 1 << 0,
    PL_CONE_M = 1 << 1,
    PL_CONE_S = 1 << 2,

    // Convenience aliases
    PL_CONE_NONE = 0,
    PL_CONE_LM   = PL_CONE_L | PL_CONE_M,
    PL_CONE_MS   = PL_CONE_M | PL_CONE_S,
    PL_CONE_LS   = PL_CONE_L | PL_CONE_S,
    PL_CONE_LMS  = PL_CONE_L | PL_CONE_M | PL_CONE_S,
};

// Structure describing parameters for simulating color blindness
struct pl_cone_params {
    enum pl_cone cones; // Which cones are *affected* by the vision model
    float strength;     // Coefficient for how strong the defect is
                        // (1.0 = Unaffected, 0.0 = Full blindness)
};

#define pl_cone_params(...) (&(struct pl_cone_params) { __VA_ARGS__ })

// Built-in color blindness models
extern const struct pl_cone_params pl_vision_normal;        // No distortion (92%)
extern const struct pl_cone_params pl_vision_protanomaly;   // Red deficiency (0.66%)
extern const struct pl_cone_params pl_vision_protanopia;    // Red absence (0.59%)
extern const struct pl_cone_params pl_vision_deuteranomaly; // Green deficiency (2.7%)
extern const struct pl_cone_params pl_vision_deuteranopia;  // Green absence (0.56%)
extern const struct pl_cone_params pl_vision_tritanomaly;   // Blue deficiency (0.01%)
extern const struct pl_cone_params pl_vision_tritanopia;    // Blue absence (0.016%)
extern const struct pl_cone_params pl_vision_monochromacy;  // Blue cones only (<0.001%)
extern const struct pl_cone_params pl_vision_achromatopsia; // Rods only (<0.0001%)

// Returns a cone adaptation matrix. Applying this to an RGB color in the given
// color space will apply the given cone adaptation coefficients for simulating
// a type of color blindness.
//
// For the color blindness models which don't entail complete loss of a cone,
// you can partially counteract the effect by using a similar model with the
// `strength` set to its inverse. For example, to partially counteract
// deuteranomaly, you could generate a cone matrix for PL_CONE_M with the
// strength 2.0 (or some other number above 1.0).
struct pl_matrix3x3 pl_get_cone_matrix(const struct pl_cone_params *params,
                                       const struct pl_raw_primaries *prim);

// Returns a color decoding matrix for a given combination of source color
// representation and adjustment parameters. This mutates `repr` to reflect the
// change. If `params` is NULL, it defaults to &pl_color_adjustment_neutral.
//
// This function always performs a conversion to RGB. To convert to other
// colorspaces (e.g. between YUV systems), obtain a second YUV->RGB matrix
// and invert it using `pl_transform3x3_invert`.
//
// Note: For BT.2020 constant-luminance, this outputs chroma information in the
// range [-0.5, 0.5]. Since the CL system conversion is non-linear, further
// processing must be done by the caller. The channel order is CrYCb.
//
// Note: For BT.2100 ICtCp, this outputs in the color space L'M'S'. Further
// non-linear processing must be done by the caller.
//
// Note: For XYZ system, the input/encoding gamma must be pre-applied by the
// user, typically this has a value of 2.6.
struct pl_transform3x3 pl_color_repr_decode(struct pl_color_repr *repr,
                                    const struct pl_color_adjustment *params);

// Common struct to describe an ICC profile
struct pl_icc_profile {
    // Points to the in-memory representation of the ICC profile. This is
    // allowed to be NULL, in which case the `pl_icc_profile` represents "no
    // profile”.
    const void *data;
    size_t len;

    // If a profile is set, this signature must uniquely identify it, ideally
    // using a checksum of the profile contents. The user is free to choose the
    // method of determining this signature, but note the existence of the
    // `pl_icc_profile_compute_signature` helper.
    uint64_t signature;
};

// This doesn't do a comparison of the actual contents, only of the signature.
bool pl_icc_profile_equal(const struct pl_icc_profile *p1,
                          const struct pl_icc_profile *p2);

// Sets `signature` to a hash of `profile->data`, if non-NULL. Provided as a
// convenience function for the sake of users ingesting arbitrary ICC profiles
// from sources where they can't reliably detect profile changes.
//
// Note: This is based on a very fast hash, and will compute a signature for
// even large (10 MB) ICC profiles in, typically, a fraction of a millisecond.
void pl_icc_profile_compute_signature(struct pl_icc_profile *profile);

PL_API_END

#endif // LIBPLACEBO_COLORSPACE_H_
