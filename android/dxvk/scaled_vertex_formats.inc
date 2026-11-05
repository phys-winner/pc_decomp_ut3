// Scaled vertex attribute fallback for Vulkan drivers without *_SCALED vertex buffer formats.
//
// D3D9 maps D3DDECLTYPE_UBYTE4, SHORT2, SHORT4 and UDEC3 to USCALED/SSCALED formats. These are
// optional for VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, and the Qualcomm proprietary driver (Adreno 7xx,
// for example 735 / driver 0x802fa029) reports none of them. DXVK's pipeline validation then drops
// every draw that uses them, which removes UE3 terrain (UBYTE4 position, SHORT2 gradients).
//
// The matching UINT/SINT formats are mandatory vertex buffer formats and fetch the same integers
// without conversion. The pipeline uses them instead and the vertex shader converts the fetched
// integers with OpConvertUToF / OpConvertSToF, which is exactly the value a SCALED fetch returns.
// Drivers that support the SCALED format keep it unchanged.
//
// Included into the DXVK tree by tools/build_dxvk.py (--rebuild) as src/dxvk/dxvk_scaled_vertex.h.
#pragma once

namespace dxvk {

  /// Integer format with the same memory layout as a SCALED format, or VK_FORMAT_UNDEFINED.
  inline VkFormat scaledVertexIntegerFormat(VkFormat format, bool* isSigned) {
    struct Entry { VkFormat scaled; VkFormat integer; bool sign; };
    static const Entry entries[] = {
      { VK_FORMAT_R8_USCALED,                 VK_FORMAT_R8_UINT,                  false },
      { VK_FORMAT_R8_SSCALED,                 VK_FORMAT_R8_SINT,                  true  },
      { VK_FORMAT_R8G8_USCALED,               VK_FORMAT_R8G8_UINT,                false },
      { VK_FORMAT_R8G8_SSCALED,               VK_FORMAT_R8G8_SINT,                true  },
      { VK_FORMAT_R8G8B8A8_USCALED,           VK_FORMAT_R8G8B8A8_UINT,            false },
      { VK_FORMAT_R8G8B8A8_SSCALED,           VK_FORMAT_R8G8B8A8_SINT,            true  },
      { VK_FORMAT_A2B10G10R10_USCALED_PACK32, VK_FORMAT_A2B10G10R10_UINT_PACK32,  false },
      { VK_FORMAT_A2B10G10R10_SSCALED_PACK32, VK_FORMAT_A2B10G10R10_SINT_PACK32,  true  },
      { VK_FORMAT_R16_USCALED,                VK_FORMAT_R16_UINT,                 false },
      { VK_FORMAT_R16_SSCALED,                VK_FORMAT_R16_SINT,                 true  },
      { VK_FORMAT_R16G16_USCALED,             VK_FORMAT_R16G16_UINT,              false },
      { VK_FORMAT_R16G16_SSCALED,             VK_FORMAT_R16G16_SINT,              true  },
      { VK_FORMAT_R16G16B16A16_USCALED,       VK_FORMAT_R16G16B16A16_UINT,        false },
      { VK_FORMAT_R16G16B16A16_SSCALED,       VK_FORMAT_R16G16B16A16_SINT,        true  },
    };

    for (const auto& e : entries) {
      if (e.scaled == format) {
        *isSigned = e.sign;
        return e.integer;
      }
    }

    return VK_FORMAT_UNDEFINED;
  }


  /// Format the pipeline really fetches; sets *conversion to 1 (unsigned) or 2 (signed) when the
  /// shader must convert, 0 otherwise.
  inline VkFormat scaledVertexFetchFormat(const DxvkDevice* device, VkFormat format, uint32_t* conversion) {
    *conversion = 0;
    bool isSigned = false;
    VkFormat integer = scaledVertexIntegerFormat(format, &isSigned);

    if (integer == VK_FORMAT_UNDEFINED
     || (device->getFormatFeatures(format).buffer & VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT)
     || !(device->getFormatFeatures(integer).buffer & VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT))
      return format;

    *conversion = isSigned ? 2 : 1;
    return integer;
  }

}
