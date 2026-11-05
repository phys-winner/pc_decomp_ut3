  // Included into src/dxvk/dxvk_shader.cpp (namespace dxvk) by tools/build_dxvk.py --rebuild.
  // See dxvk_scaled_vertex.h: the vertex input at `location` is fetched as an integer vector and
  // converted to the float vector the shader declares.
  //
  // 1. The existing float input variable becomes a Private variable with the same id
  //    (eliminateInput also retypes every access chain into it).
  // 2. A new Input variable of the integer vector type takes over the location.
  // 3. The entry function first stores ConvertUToF/ConvertSToF(load(new input)) into it.
  //
  // Called once per converted location on the same module (vsUintInputs, then vsSintInputs), so
  // the types a previous call declared must be found again: SPIR-V forbids a second declaration of
  // a non-aggregate type (two equal OpTypeInt or OpTypeVector), and those were inserted after the
  // module's own variables. Types are therefore looked up in the whole declaration section, up to
  // the first function, and the input variable separately (the first Input variable with the
  // location, the one eliminateInput takes).
  void DxvkShader::convertIntegerInput(SpirvCodeBuffer& code, uint32_t location, bool isSigned) {
    std::unordered_set<uint32_t> candidates;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> vectors; // id -> component, count
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointers; // id -> storage, type
    std::unordered_set<uint32_t> floats32;
    uint32_t intType = 0;
    uint32_t varId = 0;
    uint32_t baseType = 0;
    uint32_t entryFunction = 0;
    bool inputSeen = false;

    for (auto ins : code) {
      if (ins.opCode() == spv::OpFunction)
        break;
      if (ins.opCode() == spv::OpEntryPoint && !entryFunction)
        entryFunction = ins.arg(2);
      if (ins.opCode() == spv::OpDecorate && ins.arg(2) == spv::DecorationLocation && ins.arg(3) == location)
        candidates.insert(ins.arg(1));
      if (ins.opCode() == spv::OpTypeFloat && ins.arg(2) == 32)
        floats32.insert(ins.arg(1));
      if (ins.opCode() == spv::OpTypeInt && ins.arg(2) == 32 && ins.arg(3) == uint32_t(isSigned))
        intType = ins.arg(1);
      if (ins.opCode() == spv::OpTypeVector)
        vectors.insert({ ins.arg(1), { ins.arg(2), ins.arg(3) } });
      if (ins.opCode() == spv::OpTypePointer)
        pointers.insert({ ins.arg(1), { ins.arg(2), ins.arg(3) } });
      if (ins.opCode() == spv::OpVariable && ins.arg(3) == spv::StorageClassInput
       && candidates.count(ins.arg(2)) && !inputSeen) {
        inputSeen = true;
        auto pointer = pointers.find(ins.arg(1));
        if (pointer != pointers.end()) {
          varId = ins.arg(2);
          baseType = pointer->second.second;
        }
      }
    }

    // Only scalar or vector float32 inputs are converted; anything else keeps its fetch format.
    uint32_t count = 0;
    if (floats32.count(baseType))
      count = 1;
    else if (vectors.count(baseType) && floats32.count(vectors[baseType].first))
      count = vectors[baseType].second;

    if (!varId || !entryFunction || !count)
      return;

    eliminateInput(code, location);

    // Types and the new variable go right before the first function.
    size_t functionOffset = 0;
    for (auto ins : code) {
      if (ins.opCode() == spv::OpFunction) {
        functionOffset = ins.offset();
        break;
      }
    }
    if (!functionOffset)
      return;

    code.beginInsertion(functionOffset);
    if (!intType) {
      intType = code.allocId();
      code.putIns(spv::OpTypeInt, 4);
      code.putWord(intType);
      code.putWord(32);
      code.putWord(uint32_t(isSigned));
    }

    uint32_t fetchType = intType;
    if (count > 1) {
      fetchType = 0;
      for (const auto& v : vectors) {
        if (v.second.first == intType && v.second.second == count)
          fetchType = v.first;
      }
      if (!fetchType) {
        fetchType = code.allocId();
        code.putIns(spv::OpTypeVector, 4);
        code.putWord(fetchType);
        code.putWord(intType);
        code.putWord(count);
      }
    }

    uint32_t pointerType = 0;
    for (const auto& p : pointers) {
      if (p.second.first == spv::StorageClassInput && p.second.second == fetchType)
        pointerType = p.first;
    }
    if (!pointerType) {
      pointerType = code.allocId();
      code.putIns(spv::OpTypePointer, 4);
      code.putWord(pointerType);
      code.putWord(spv::StorageClassInput);
      code.putWord(fetchType);
    }

    uint32_t fetchVar = code.allocId();
    code.putIns(spv::OpVariable, 4);
    code.putWord(pointerType);
    code.putWord(fetchVar);
    code.putWord(spv::StorageClassInput);
    code.endInsertion();

    // Location decoration, in the annotation section.
    for (auto ins : code) {
      if (ins.opCode() == spv::OpDecorate || ins.opCode() == spv::OpMemberDecorate) {
        code.beginInsertion(ins.offset());
        code.putIns(spv::OpDecorate, 4);
        code.putWord(fetchVar);
        code.putWord(spv::DecorationLocation);
        code.putWord(location);
        code.endInsertion();
        break;
      }
    }

    // Interface of the entry point.
    for (auto ins : code) {
      if (ins.opCode() == spv::OpEntryPoint) {
        uint32_t length = ins.length();
        ins.setArg(0, spv::OpEntryPoint | ((length + 1) << spv::WordCountShift));
        code.beginInsertion(ins.offset() + length);
        code.putWord(fetchVar);
        code.endInsertion();
        break;
      }
    }

    // Conversion before anything else of the entry function runs.
    bool inEntry = false;
    bool afterLabel = false;
    for (auto ins : code) {
      if (ins.opCode() == spv::OpFunction)
        inEntry = ins.arg(2) == entryFunction;
      else if (inEntry && ins.opCode() == spv::OpLabel)
        afterLabel = true;
      else if (afterLabel && ins.opCode() != spv::OpVariable
            && ins.opCode() != spv::OpLine && ins.opCode() != spv::OpNoLine) {
        uint32_t loaded = code.allocId();
        uint32_t converted = code.allocId();

        code.beginInsertion(ins.offset());
        code.putIns(spv::OpLoad, 4);
        code.putWord(fetchType);
        code.putWord(loaded);
        code.putWord(fetchVar);
        code.putIns(isSigned ? spv::OpConvertSToF : spv::OpConvertUToF, 4);
        code.putWord(baseType);
        code.putWord(converted);
        code.putWord(loaded);
        code.putIns(spv::OpStore, 3);
        code.putWord(varId);
        code.putWord(converted);
        code.endInsertion();
        break;
      }
    }
  }
