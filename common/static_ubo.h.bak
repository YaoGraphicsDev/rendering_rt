#pragma once

#include "otcv.h"
#include <cassert>

struct Std140AlignmentType {
	enum class InlineType {
		Vec4,
		Vec3,
		Vec2,
		Float,
		Uint,
		Int,
		Mat4,
		Bool
	};
	Std140AlignmentType& add(InlineType type, const std::string& name, uint32_t array_count = 1);

	Std140AlignmentType& add(Std140AlignmentType& custom_type, const std::string& name, uint32_t array_count = 1);

	struct Range {
		VkDeviceSize offset;
		VkDeviceSize stride;
		VkDeviceSize size;
	};

	std::vector<Range> _field_ranges;
	std::map<std::string, size_t> _field_name_range_map; // maps name to _field_ranges index

	std::map<std::string, InlineType> _field_name_inline_type_map;
	std::map<std::string, Std140AlignmentType> _field_name_custom_type_map;

	VkDeviceSize _total_size = 0;
};

struct Std430AlignmentType {
	enum class InlineType {
		Vec4,
		Vec3,
		Vec2,
		Float,
		Uint,
		Int,
		Mat4,
		Bool
	};
	Std430AlignmentType& add(InlineType type, const std::string& name, uint32_t array_count = 1);

	Std430AlignmentType& add(Std430AlignmentType& custom_type, const std::string& name, uint32_t array_count = 1);

	struct Range {
		VkDeviceSize offset;
		VkDeviceSize stride;
		VkDeviceSize size;
	};

	std::vector<Range> _field_ranges;
	std::map<std::string, size_t> _field_name_range_map; // maps name to _field_ranges index

	std::map<std::string, InlineType> _field_name_inline_type_map;
	std::map<std::string, Std430AlignmentType> _field_name_custom_type_map;

	VkDeviceSize _total_size = 0;
	uint32_t _max_base_alignment = 0;
};


struct BufferObjectAccess {
	BufferObjectAccess& operator[](const std::string& name) {
		_path.push_back({ name, 0 });
		return *this;
	}
	BufferObjectAccess& operator[](uint32_t id) {
		assert(!_path.empty());
		assert(!_path.back().field_name.empty());
		_path.back().id = id;
		return *this;
	}
	void clear() {
		_path.clear();
	}

	struct Access {
		std::string field_name;
		uint32_t id;
	};
	std::vector<Access> _path;
};

typedef BufferObjectAccess StaticUBOAccess;
typedef BufferObjectAccess SSBOAccess;


struct StaticUBO {
	StaticUBO(const Std140AlignmentType& layout);
	~StaticUBO();

	void set(StaticUBOAccess& access, const void* value);

	otcv::Buffer* _buf;
	Std140AlignmentType _layout;
};

struct StaticUBOArray {
	StaticUBOArray(const Std140AlignmentType& layout, uint32_t n_ubos, uint32_t ubo_alignment);
	~StaticUBOArray();

	void set(uint32_t ubo_id, StaticUBOAccess& access, const void* value);

	uint32_t _stride;
	uint32_t _n_ubos;
	otcv::Buffer* _buf;
	Std140AlignmentType _layout;
};

struct SSBO {
	SSBO(const Std430AlignmentType& layout, uint32_t n_ssbo, VkBufferUsageFlags additional_usage = 0);
	~SSBO();

	struct WriteContext {
		uint32_t id;
		struct AccessContext {
			SSBOAccess acc;
			const void* value;
		};
		std::vector<AccessContext> access_ctxs;
	};
	// should only be used for initializtion. As it idle waits for transfer to finish
	// provide asyn version
	void write(std::vector<WriteContext>& writes);

	Std430AlignmentType::Range range_of(uint32_t id, SSBOAccess& acc);

	uint32_t _stride;
	uint32_t _n_ssbos;
	otcv::Buffer* _buf;
	std::vector<uint8_t> _staging_buf;
	Std430AlignmentType _layout;
};

struct SSBOLayout {
	SSBOLayout() {};

	SSBOLayout(const Std430AlignmentType& layout, uint32_t n_ssbo);

	struct WriteContext {
		uint32_t id;
		struct AccessContext {
			SSBOAccess acc;
			const void* value;
		};
		std::vector<AccessContext> access_ctxs;
	};

	Std430AlignmentType::Range range_of(uint32_t id, SSBOAccess& acc);

	uint32_t _stride;
	uint32_t _n_ssbos;
	otcv::BufferBuilder _builder;
	Std430AlignmentType _layout;
};