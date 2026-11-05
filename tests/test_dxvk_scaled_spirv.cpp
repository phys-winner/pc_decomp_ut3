/* DXVK's SCALED vertex fallback (android/dxvk/scaled_vertex_spirv.inc) applied to a SPIR-V module the
 * way DXVK applies it: convertIntegerInput (which calls the upstream eliminateInput) once per converted
 * location, in turn, on the same code. Built and driven by tests/test_dxvk_scaled_spirv.py against
 * build/dxvk-source: SpirvCodeBuffer and eliminateInput are DXVK's own, the overlay is the one under test.
 * usage: test_dxvk_scaled_spirv in.spv out.spv {u|s}LOCATION... */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "spirv/spirv_code_buffer.h"
#include "spirv/spirv_code_buffer.cpp"
#include "spirv/spirv_module.h"         /* spvVersion, used by eliminateInput */

namespace dxvk {

  class DxvkShader {
  public:
    static void convertIntegerInput(SpirvCodeBuffer& code, uint32_t location, bool isSigned);
    static void eliminateInput(SpirvCodeBuffer& code, uint32_t location);
  };

#include "eliminate_input.inc"
#include "scaled_vertex_spirv.inc"

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s in.spv out.spv {u|s}LOCATION...\n", argv[0]);
    return 2;
  }
  std::FILE* in = std::fopen(argv[1], "rb");
  if (!in)
    return 2;
  std::vector<uint32_t> words;
  uint32_t word;
  while (std::fread(&word, sizeof(word), 1, in) == 1)
    words.push_back(word);
  std::fclose(in);

  dxvk::SpirvCodeBuffer code(uint32_t(words.size()), words.data());
  for (int i = 3; i < argc; i++) {
    if (argv[i][0] != 'u' && argv[i][0] != 's')
      return 2;
    dxvk::DxvkShader::convertIntegerInput(code, uint32_t(std::strtoul(argv[i] + 1, nullptr, 10)), argv[i][0] == 's');
  }

  std::FILE* out = std::fopen(argv[2], "wb");
  if (!out)
    return 2;
  size_t written = std::fwrite(code.data(), sizeof(uint32_t), code.dwords(), out);
  std::fclose(out);
  return written == code.dwords() ? 0 : 2;
}
