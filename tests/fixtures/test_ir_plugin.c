#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "machine/plugins/abi/TimePluginAbi.h"

#define TEST_ARCHITECTURE_ID 0x5A800001u
#ifndef TEST_IR_ABI_VERSION
#define TEST_IR_ABI_VERSION TIME_IR_ABI_VERSION_V1
#endif

struct TestIrInstance {
    const char* error;
};

struct TestArtifact {
    uint64_t next_pc;
};

static void* create_instance(const struct TimeHostApiV1* host)
{
    struct TestIrInstance* instance;
    if (host == NULL || host->struct_size < sizeof(*host) ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1) return NULL;
    instance = (struct TestIrInstance*)calloc(1u, sizeof(*instance));
    return instance;
}

static void destroy_instance(void* instance) { free(instance); }

static int32_t lower_nop(void* opaque,
                         const struct TimeIrLoweringRequestV1* request,
                         const struct TimeIrBuilderV1* builder)
{
    struct TestIrInstance* instance = (struct TestIrInstance*)opaque;
    struct TimeIrGuardV1 guard;
    struct TimeIrInstructionV1 instruction;
    struct TimeIrOperandV1 operand;
    struct TimeIrOperationV1 operation;
    uint64_t next_pc;
    if (instance == NULL || request == NULL || builder == NULL ||
        request->struct_size < sizeof(*request) ||
        request->ir_abi_version != TIME_IR_ABI_VERSION_V1 ||
        request->instruction_count == 0u || request->instructions == NULL ||
        builder->struct_size < sizeof(*builder) ||
        builder->ir_abi_version != TIME_IR_ABI_VERSION_V1) return TIME_IR_ERROR_V1;
    if (request->instructions[0].length != 1u ||
        request->instructions[0].bytes[0] != 0x00u) {
        instance->error = "fixture adapter supports only NOP";
        return TIME_IR_DECLINED_V1;
    }
    if (builder->begin_block(builder->host_context,
                             request->instructions[0].address,
                             request->mapping_generation) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    memset(&guard, 0, sizeof(guard));
    guard.struct_size = sizeof(guard);
    guard.kind = TIME_IR_GUARD_MAPPING_GENERATION_V1;
    guard.expected = request->mapping_generation;
    guard.mask = UINT64_MAX;
    if (builder->add_guard(builder->host_context, &guard) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
#if !defined(TEST_IR_OMIT_HELPER_GUARD)
    guard.kind = TIME_IR_GUARD_HELPER_ABI_V1;
    guard.subject = 0u;
    guard.expected = 1u;
    guard.mask = UINT64_MAX;
    guard.byte_count = 0u;
    guard.bytes = NULL;
    if (builder->add_guard(builder->host_context, &guard) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
#endif
    guard.kind = TIME_IR_GUARD_EXECUTION_STATE_V1;
    guard.subject = 0u;
    guard.expected = request->execution_state;
    guard.mask = 0x7u;
    if (builder->add_guard(builder->host_context, &guard) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    guard.kind = TIME_IR_GUARD_CODE_BYTES_V1;
    guard.subject = request->instructions[0].address;
    guard.expected = 0u;
    guard.mask = UINT64_MAX;
    guard.byte_count = 1u;
    guard.bytes = request->instructions[0].bytes;
    if (builder->add_guard(builder->host_context, &guard) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    memset(&instruction, 0, sizeof(instruction));
    instruction.struct_size = sizeof(instruction);
    instruction.address = request->instructions[0].address;
    instruction.length = 1u;
    instruction.cycles_not_taken = 4u;
    instruction.cycles_taken = 4u;
    if (builder->begin_instruction(builder->host_context, &instruction) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    memset(&operand, 0, sizeof(operand));
    operand.struct_size = sizeof(operand);
    operand.kind = TIME_IR_OPERAND_HELPER_V1;
    operand.type = TIME_IR_VALUE_VOID_V1;
    operand.payload = 0u;
    memset(&operation, 0, sizeof(operation));
    operation.struct_size = sizeof(operation);
    operation.opcode = TIME_IR_OPCODE_CALL_HELPER_V1;
    operation.result_type = TIME_IR_VALUE_VOID_V1;
    operation.memory_class = TIME_IR_MEMORY_GENERIC_V1;
    operation.operand_count = 1u;
    operation.operands = &operand;
    if (builder->emit_operation(builder->host_context, &operation) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    next_pc = instruction.address + instruction.length;
    operand.struct_size = sizeof(operand);
    operand.kind = TIME_IR_OPERAND_IMMEDIATE_V1;
    operand.type = TIME_IR_VALUE_I16_V1;
    operand.payload = next_pc;
    memset(&operation, 0, sizeof(operation));
    operation.struct_size = sizeof(operation);
    operation.opcode = TIME_IR_OPCODE_SET_PROGRAM_COUNTER_V1;
    operation.result_type = TIME_IR_VALUE_VOID_V1;
    operation.memory_class = TIME_IR_MEMORY_GENERIC_V1;
    operation.operand_count = 1u;
    operation.operands = &operand;
    if (builder->emit_operation(builder->host_context, &operation) != TIME_IR_OK_V1 ||
        builder->end_instruction(builder->host_context) != TIME_IR_OK_V1 ||
        builder->finish_block(builder->host_context,
                              TIME_IR_BLOCK_EXIT_SEQUENTIAL_V1) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    instance->error = NULL;
    fprintf(stderr, "fixture-ir: adapter-lowered\n");
    return TIME_IR_OK_V1;
}

static int32_t validate_block(const void* opaque, const struct TimeIrBlockViewV1* block)
{
    const struct TestIrInstance* instance = (const struct TestIrInstance*)opaque;
    (void)instance;
    if (block == NULL || block->struct_size < sizeof(*block) ||
        block->ir_abi_version != TIME_IR_ABI_VERSION_V1 ||
        block->instruction_count != 1u || block->instructions == NULL ||
        block->instructions[0].operation_count != 3u ||
        block->instructions[0].operations == NULL ||
        block->instructions[0].operations[0].operand_count != 1u ||
        block->instructions[0].operations[0].operands == NULL ||
        block->instructions[0].operations[0].opcode != TIME_IR_OPCODE_CALL_HELPER_V1 ||
        block->instructions[0].operations[2].opcode !=
            TIME_IR_OPCODE_RETIRE_INSTRUCTION_V1) return TIME_IR_ERROR_V1;
    return TIME_IR_OK_V1;
}

static int32_t validate_state(const void* opaque, const struct TimeIrBlockViewV1* block)
{
    (void)opaque;
    return block != NULL ? TIME_IR_OK_V1 : TIME_IR_ERROR_V1;
}

static const char* last_error(const void* opaque)
{
    const struct TestIrInstance* instance = (const struct TestIrInstance*)opaque;
    return instance != NULL ? instance->error : "null fixture instance";
}

static void* compile_block(void* opaque, const struct TimeIrBlockViewV1* block)
{
    struct TestArtifact* artifact;
    if (validate_block(opaque, block) != TIME_IR_OK_V1) return NULL;
    artifact = (struct TestArtifact*)malloc(sizeof(*artifact));
    if (artifact != NULL) artifact->next_pc = block->guest_end + 1u;
    return artifact;
}

static void destroy_artifact(void* opaque, void* artifact)
{
    (void)opaque;
    free(artifact);
}

static int32_t execute_block(void* opaque, void* raw_artifact,
                             uint32_t instruction_index,
                             const struct TimeIrExecutionHostV1* host,
                             struct TimeIrExecutionResultV1* result)
{
    const struct TestArtifact* artifact = (const struct TestArtifact*)raw_artifact;
    (void)opaque;
    if (artifact == NULL || instruction_index != 0u || host == NULL || result == NULL ||
        host->struct_size < sizeof(*host) || host->ir_abi_version != TIME_IR_ABI_VERSION_V1 ||
        result->struct_size < sizeof(*result) || host->set_program_counter == NULL ||
        host->set_program_counter(host->host_context, artifact->next_pc) != TIME_IR_OK_V1) {
        return TIME_IR_ERROR_V1;
    }
    result->branch_taken = 0u;
    result->exit_requested = 0u;
    result->cycle_condition = 0u;
    result->retirement_reached = 1u;
    fprintf(stderr, "fixture-ir: backend-executed\n");
    return TIME_IR_OK_V1;
}

static const struct TimeIrCoreAdapterApiV1 adapter_api = {
    sizeof(struct TimeIrCoreAdapterApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    TEST_ARCHITECTURE_ID, TEST_IR_ABI_VERSION,
    create_instance, destroy_instance, lower_nop, validate_block, validate_state, last_error};

static const struct TimeIrExecutionBackendApiV1 backend_api = {
    sizeof(struct TimeIrExecutionBackendApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    0u, TEST_IR_ABI_VERSION, create_instance, destroy_instance,
    compile_block, destroy_artifact, execute_block, last_error};

static const struct TimePluginDescriptorV1 descriptors[] = {
    {sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_IR_CORE_ADAPTER_V1,
     "test.ir-adapter.gamegear-nop", "C Game Gear NOP Adapter",
     &adapter_api, sizeof(adapter_api)},
    {sizeof(struct TimePluginDescriptorV1), TIME_PLUGIN_KIND_IR_EXECUTION_BACKEND_V1,
     "test.ir-backend.host-callback", "C Host Callback Backend",
     &backend_api, sizeof(backend_api)},
};

static const struct TimePluginDescriptorV1* plugin_at(uint32_t index)
{
#if defined(TEST_IR_ADAPTER_ONLY)
    return index == 0u ? &descriptors[0] : NULL;
#elif defined(TEST_IR_BACKEND_ONLY)
    return index == 0u ? &descriptors[1] : NULL;
#else
    return index < 2u ? &descriptors[index] : NULL;
#endif
}

#if defined(TEST_IR_ADAPTER_ONLY) || defined(TEST_IR_BACKEND_ONLY)
#define TEST_PLUGIN_COUNT 1u
#else
#define TEST_PLUGIN_COUNT 2u
#endif

static const struct TimePluginModuleV1 module = {
    sizeof(struct TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "test.module.ir", "C IR Test Module", TEST_PLUGIN_COUNT, plugin_at};

TIME_PLUGIN_EXPORT const struct TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &module;
}
