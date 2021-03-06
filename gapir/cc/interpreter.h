/*
 * Copyright (C) 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GAPIR_INTERPRETER_H
#define GAPIR_INTERPRETER_H

#include "function_table.h"
#include "stack.h"

#include <stdint.h>

#include <functional>
#include <unordered_map>
#include <utility>

namespace gapir {

class MemoryManager;

// Implementation of a (fix sized) stack based virtual machine to interpret the instructions in the
// given opcode stream.
class Interpreter {
public:
    // The type of the callback function for requesting to register an api's renderer functions to
    // this interpreter. Taking in the pointer for the interpreter and the api index, the callback
    // is expected to populate the renderer function for the given api index in the interpreter.
    // It should return true if the request is fulfilled.
    using ApiRequestCallback = std::function<bool(Interpreter*, uint8_t)>;

    // Function ids for implementation specific functions and special debugging functions. These
    // functions shouldn't be called by the opcode stream
    enum FunctionIds : uint16_t {
        // Custom function Ids
        POST_FUNCTION_ID        = 0xff00,
        RESOURCE_FUNCTION_ID    = 0xff01,
        // Debug function Ids
        PRINT_STACK_FUNCTION_ID = 0xff80,
        // 0xff81..0xffff reserved for synthetic functions
    };

    // Instruction codes for the different instructions. The codes have to be consistent with the
    // codes on the server side.
    enum class InstructionCode : uint8_t {
        CALL        = 0,
        PUSH_I      = 1,
        LOAD_C      = 2,
        LOAD_V      = 3,
        LOAD        = 4,
        POP         = 5,
        STORE_V     = 6,
        STORE       = 7,
        RESOURCE    = 8,
        POST        = 9,
        COPY        = 10,
        CLONE       = 11,
        STRCPY      = 12,
        EXTEND      = 13,
        ADD         = 14,
        LABEL       = 15,
    };

    // Creates a new interpreter with the specified memory manager (for resolving memory addresses)
    // and with the specified maximum stack size
    Interpreter(const MemoryManager* memoryManager, uint32_t stackDepth,
                ApiRequestCallback callback);

    // Registers a builtin function to the builtin function table.
    void registerBuiltin(FunctionTable::Id, FunctionTable::Function);

    // Assigns the function table as the renderer functions to use for the given api.
    void setRendererFunctions(uint8_t api, FunctionTable* functionTable);

    // Runs the interpreter on the instruction list specified by the pointer and by its size.
    bool run(const std::pair<const uint32_t*, uint32_t>& instructions);

    // Registers an API instance if it has not already been done.
    bool registerApi(uint8_t api);

    // Returns the last reached label value.
    inline uint32_t getLabel() const;

private:
    enum : uint32_t {
        TYPE_MASK        = 0x03f00000U,
        FUNCTION_ID_MASK = 0x0000ffffU,
        API_INDEX_MASK   = 0x000f0000U,
        PUSH_RETURN_MASK = 0x01000000U,
        DATA_MASK20      = 0x000fffffU,
        DATA_MASK26      = 0x03ffffffU,
        API_BIT_SHIFT    = 16,
        TYPE_BIT_SHIFT   = 20,
        OPCODE_BIT_SHIFT = 26,
    };

    // Get type information out from an opcode. The type is always stored in the 7th to 13th MSB
    // (both inclusive) of the opcode
    BaseType extractType(uint32_t opcode) const;

    // Get 20 bit data out from an opcode located in the 20 LSB of the opcode.
    uint32_t extract20bitData(uint32_t opcode) const;

    // Get 26 bit data out from an opcode located in the 26 LSB of the opcode.
    uint32_t extract26bitData(uint32_t opcode) const;

    // Implementation of the opcodes supported by the interpreter. Each function returns true if the
    // operation was successful, false otherwise
    bool call(uint32_t opcode);
    bool pushI(uint32_t opcode);
    bool loadC(uint32_t opcode);
    bool loadV(uint32_t opcode);
    bool load(uint32_t opcode);
    bool pop(uint32_t opcode);
    bool storeV(uint32_t opcode);
    bool store();
    bool resource(uint32_t);
    bool post();
    bool copy(uint32_t opcode);
    bool clone(uint32_t opcode);
    bool strcpy(uint32_t opcode);
    bool extend(uint32_t opcode);
    bool add(uint32_t opcode);
    bool label(uint32_t opcode);

    // Returns true, if address..address+size(type) is "constant" memory.
    bool isConstantAddressForType(const void *address, BaseType type) const;

    // Returns true, if address..address+size(type) is "volatile" memory.
    bool isVolatileAddressForType(const void *address, BaseType type) const;

    // Returns false, if address is known not safe to read from.
    bool isReadAddress(const void * address) const;

    // Returns false, if address is known not safe to write to.
    bool isWriteAddress(void* address) const;

    // Interpret one specific opcode. Returns true if it was successful false otherwise
    bool interpret(uint32_t opcode);

    // Memory manager which managing the memory used during the interpretation
    const MemoryManager* mMemoryManager;

    // The builtin functions.
    FunctionTable mBuiltins;

    // The current renderer functions.
    std::unordered_map<uint8_t, FunctionTable*> mRendererFunctions;

    // Callback function for requesting renderer functions for an unknown api.
    ApiRequestCallback apiRequestCallback;

    // The stack of the Virtual Machine
    Stack mStack;

    // The last reached label value.
    uint32_t mLabel;
};

inline bool Interpreter::isConstantAddressForType(const void *address, BaseType type) const {
    // Treat all pointer types as sizeof(void*)
    size_t size = isPointerType(type) ? sizeof(void*) : baseTypeSize(type);
    return mMemoryManager->isConstantAddressWithSize(address, size);
}

inline bool Interpreter::isVolatileAddressForType(const void *address, BaseType type) const {
    size_t size = isPointerType(type) ? sizeof(void*) : baseTypeSize(type);
    return mMemoryManager->isVolatileAddressWithSize(address, baseTypeSize(type));
}

inline bool Interpreter::isReadAddress(const void * address) const {
    return address != nullptr && !mMemoryManager->isNotObservedAbsoluteAddress(address);
}

inline bool Interpreter::isWriteAddress(void* address) const {
    return address != nullptr &&
            !mMemoryManager->isNotObservedAbsoluteAddress(address) &&
            !mMemoryManager->isConstantAddress(address);
}

inline uint32_t Interpreter::getLabel() const {
    return mLabel;
}

}  // namespace gapir

#endif  // GAPIR_INTERPRETER_H
