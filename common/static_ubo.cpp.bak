#include "static_ubo.h"
#include <cassert>

uint32_t get_base_alignment(Std140AlignmentType::InlineType type, bool in_array) {
    switch (type) {
    case Std140AlignmentType::InlineType::Float:
    case Std140AlignmentType::InlineType::Uint:
    case Std140AlignmentType::InlineType::Int:
    case Std140AlignmentType::InlineType::Bool:
        return in_array ? 16 : 4;
    case Std140AlignmentType::InlineType::Vec2:
        return in_array ? 16 : 8;
    case Std140AlignmentType::InlineType::Vec3:
    case Std140AlignmentType::InlineType::Vec4:
    case Std140AlignmentType::InlineType::Mat4:
        return 16;
    default:
        assert(false);
        return in_array ? 16 : 4;
    }
}

uint32_t get_size(Std140AlignmentType::InlineType type, bool in_array) {
    switch (type) {
    case Std140AlignmentType::InlineType::Float:
    case Std140AlignmentType::InlineType::Uint:
    case Std140AlignmentType::InlineType::Int:
    case Std140AlignmentType::InlineType::Bool:
        return in_array ? 16 : 4;
    case Std140AlignmentType::InlineType::Vec2:
        return in_array ? 16 : 8;
    case Std140AlignmentType::InlineType::Vec3:
        return in_array ? 16 : 12;
    case Std140AlignmentType::InlineType::Vec4:
        return 16;
    case Std140AlignmentType::InlineType::Mat4:
        return 64;
    default:
        assert(false);
        return in_array ? 16 : 4;
    }
}

uint32_t get_base_alignment(Std430AlignmentType::InlineType type) {
    switch (type) {
    case Std430AlignmentType::InlineType::Float:
    case Std430AlignmentType::InlineType::Uint:
    case Std430AlignmentType::InlineType::Int:
    case Std430AlignmentType::InlineType::Bool:
        return 4;
    case Std430AlignmentType::InlineType::Vec2:
        return 8;
    case Std430AlignmentType::InlineType::Vec3:
    case Std430AlignmentType::InlineType::Vec4:
        return 16;
    case Std430AlignmentType::InlineType::Mat4:
        return 16; // mat4 = array of vec4
    default:
        assert(false);
        return 4;
    }
}

uint32_t get_size(Std430AlignmentType::InlineType type, bool in_array) {
    switch (type) {
    case Std430AlignmentType::InlineType::Float:
    case Std430AlignmentType::InlineType::Uint:
    case Std430AlignmentType::InlineType::Int:
    case Std430AlignmentType::InlineType::Bool:
        return 4;
    case Std430AlignmentType::InlineType::Vec2:
        return 8;
    case Std430AlignmentType::InlineType::Vec3:
        return in_array ? 16 : 12; // padded to 16 only in arrays
    case Std430AlignmentType::InlineType::Vec4:
        return 16;
    case Std430AlignmentType::InlineType::Mat4:
        return 64; // 4 × vec4
    default:
        assert(false);
        return 4;
    }
}

uint32_t round_up_to(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

Std140AlignmentType& Std140AlignmentType::add(InlineType type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = get_base_alignment(type, array_count > 1);

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = get_size(type, array_count > 1);
    range.size = range.stride * array_count;
    
    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_inline_type_map[name] = type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

Std140AlignmentType& Std140AlignmentType::add(Std140AlignmentType& custom_type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = 16;

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = round_up_to(custom_type._total_size, 16);
    range.size = range.stride * array_count;

    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_custom_type_map[name] = custom_type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

Std430AlignmentType& Std430AlignmentType::add(InlineType type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = get_base_alignment(type);
    _max_base_alignment = std::max(_max_base_alignment, base_alignment);

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = get_size(type, array_count > 1);
    range.size = range.stride * array_count;

    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_inline_type_map[name] = type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

Std430AlignmentType& Std430AlignmentType::add(Std430AlignmentType& custom_type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = custom_type._max_base_alignment;
    _max_base_alignment = std::max(_max_base_alignment, base_alignment);

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = round_up_to(custom_type._total_size, base_alignment);
    range.size = range.stride * array_count;

    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_custom_type_map[name] = custom_type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

template<typename T>
typename T::Range find_range_recursive(T& type, BufferObjectAccess& access, size_t depth) {
    if (depth >= access._path.size()) {
        // end of access path reached
        return { 0, 0, 0 };
    }

    StaticUBOAccess::Access& acc = access._path[depth];
    assert(type._field_name_range_map.find(acc.field_name) != type._field_name_range_map.end());
    T::Range& range = type._field_ranges[type._field_name_range_map[acc.field_name]];

    auto& inline_type_iter = type._field_name_inline_type_map.find(acc.field_name);
    if (inline_type_iter != type._field_name_inline_type_map.end()) {
        // inline type found
        T::InlineType type = inline_type_iter->second;
        VkDeviceSize offset = range.offset + acc.id * range.stride;
        VkDeviceSize size = get_size(type, false);
        VkDeviceSize stride = range.stride;
        return { offset, stride, size };
    }

    auto& custom_type_iter = type._field_name_custom_type_map.find(acc.field_name);
    if (custom_type_iter != type._field_name_custom_type_map.end()) {
        // custom type found
        T& custom_type = custom_type_iter->second;
        T::Range sub_range = find_range_recursive(custom_type, access, ++depth);
        VkDeviceSize offset = range.offset + acc.id * range.stride + sub_range.offset;
        VkDeviceSize size = (sub_range.size == 0 ? range.size : sub_range.size);
        VkDeviceSize stride = (sub_range.stride == 0 ? range.stride : sub_range.stride);
        return { offset, stride, size };
    }

    // acc.field_name does not exist
    assert(false);
    return {0, 0, 0};
}

StaticUBO::StaticUBO(const Std140AlignmentType& layout) {
    otcv::BufferBuilder bb;
    bb.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .host_access(otcv::BufferBuilder::Access::Coherent)
        .size(layout._total_size);
    _buf = new otcv::Buffer(bb);
    _layout = layout;
}

StaticUBO::~StaticUBO() {
    delete _buf;
    _buf = nullptr;
}

void StaticUBO::set(StaticUBOAccess& access, const void* value) {
    Std140AlignmentType::Range range = find_range_recursive(_layout, access, 0);

    assert(_buf->mapped);
    std::memcpy((char*)_buf->mapped + range.offset, value, range.size);
}

StaticUBOArray::StaticUBOArray(const Std140AlignmentType& layout, uint32_t n_ubos, uint32_t ubo_alignment) {
    _stride = round_up_to(layout._total_size, ubo_alignment);
    _n_ubos = n_ubos;

    otcv::BufferBuilder bb;
    bb.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .host_access(otcv::BufferBuilder::Access::Coherent)
        .size(_stride * _n_ubos);
    _buf = new otcv::Buffer(bb);
    _layout = layout;
}

StaticUBOArray::~StaticUBOArray() {
    delete _buf;
    _buf = nullptr;
}

void StaticUBOArray::set(uint32_t ubo_id, StaticUBOAccess& access, const void* value) {
    assert(ubo_id < _n_ubos);
    Std140AlignmentType::Range range = find_range_recursive(_layout, access, 0);

    assert(_buf->mapped);
    assert(_stride * ubo_id + range.offset < _buf->builder._info.size);
    std::memcpy((char*)_buf->mapped + _stride * ubo_id + range.offset, value, range.size);
}


SSBO::SSBO(const Std430AlignmentType& layout, uint32_t n_ssbo, VkBufferUsageFlags additional_usage) {
    _stride = round_up_to(layout._total_size, layout._max_base_alignment);
    _n_ssbos = n_ssbo;

    otcv::BufferBuilder bb;
    bb.usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additional_usage)
        .host_access(otcv::BufferBuilder::Access::Invisible)
        .size(_stride * _n_ssbos);
    _buf = new otcv::Buffer(bb);
    _staging_buf.resize(_buf->builder._info.size);
    _layout = layout;
}

SSBO::~SSBO() {
    delete _buf;
    _buf = nullptr;
}

void SSBO::write(std::vector<WriteContext>& writes) {
    for (WriteContext& write : writes) {
        assert(write.id < _n_ssbos);
        for (WriteContext::AccessContext& acc_ctx : write.access_ctxs) {
            BufferObjectAccess& acc = acc_ctx.acc;
            const void* value = acc_ctx.value;
            Std430AlignmentType::Range range = find_range_recursive(_layout, acc, 0);
            assert(_stride * write.id + range.offset + range.size <= _staging_buf.size());
            std::memcpy(_staging_buf.data() + _stride * write.id + range.offset, value, range.size);
        }
    }

    _buf->populate(_staging_buf.data());
}

Std430AlignmentType::Range SSBO::range_of(uint32_t id, SSBOAccess& acc) {
    assert(id < _n_ssbos);
    Std430AlignmentType::Range range = find_range_recursive(_layout, acc, 0);
    range.offset += _stride * id;
    range.stride = (range.stride == 0 ? _stride : range.stride);
    range.size = (range.size == 0 ? _layout._total_size : range.size);
    return range;
}

SSBOLayout::SSBOLayout(const Std430AlignmentType& layout, uint32_t n_ssbo) {
    _stride = round_up_to(layout._total_size, layout._max_base_alignment);
    _n_ssbos = n_ssbo;
    _builder.host_access(otcv::BufferBuilder::Access::Invisible).size(_stride * _n_ssbos);
    _layout = layout;
}

Std430AlignmentType::Range SSBOLayout::range_of(uint32_t id, SSBOAccess& acc) {
    assert(id < _n_ssbos);
    Std430AlignmentType::Range range = find_range_recursive(_layout, acc, 0);
    range.offset += _stride * id;
    range.stride = (range.stride == 0 ? _stride : range.stride);
    range.size = (range.size == 0 ? _layout._total_size : range.size);
    return range;
}
