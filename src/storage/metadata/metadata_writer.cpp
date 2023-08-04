#include "duckdb/storage/metadata/metadata_writer.hpp"
#include "duckdb/storage/block_manager.hpp"

namespace duckdb {

MetadataWriter::MetadataWriter(MetadataManager &manager) :
	manager(manager), capacity(0), offset(0) {

}

BlockPointer MetadataWriter::GetBlockPointer() {
	return MetadataManager::ToBlockPointer(GetMetaBlockPointer());
}

MetaBlockPointer MetadataWriter::GetMetaBlockPointer() {
	if (offset >= capacity) {
		// at the end of the block - fetch the next block
		NextBlock();
		D_ASSERT(capacity > 0);
	}
	return manager.GetDiskPointer(block.pointer, offset);
}

MetadataHandle MetadataWriter::NextHandle() {
	return manager.AllocateHandle();
}

void MetadataWriter::NextBlock() {
	// now we need to get a new block id
	auto new_handle = NextHandle();

	// write the block id of the new block to the start of the current block
	if (capacity > 0) {
		Store<idx_t>(manager.GetDiskPointer(new_handle.pointer).block_pointer, BasePtr());
	}
	// now update the block id of the block
	block = std::move(new_handle);
	current_pointer = block.pointer;
	offset = sizeof(idx_t);
	capacity = MetadataManager::METADATA_BLOCK_SIZE;
	Store<idx_t>(-1, BasePtr());
}

void MetadataWriter::WriteData(const_data_ptr_t buffer, idx_t write_size) {
	while (offset + write_size > capacity) {
		// we need to make a new block
		// first copy what we can
		D_ASSERT(offset <= capacity);
		idx_t copy_amount = capacity - offset;
		if (copy_amount > 0) {
			memcpy(Ptr(), buffer, copy_amount);
			buffer += copy_amount;
			offset += copy_amount;
			write_size -= copy_amount;
		}
		// move forward to the next block
		NextBlock();
	}
	memcpy(Ptr(), buffer, write_size);
	offset += write_size;
}

void MetadataWriter::Flush() {
	if (offset < capacity) {
		// clear remaining bytes of block (if any)
		memset(Ptr(), 0, capacity - offset);
	}
}

data_ptr_t MetadataWriter::BasePtr() {
	return block.handle.Ptr() + current_pointer.index * MetadataManager::METADATA_BLOCK_SIZE;
}

data_ptr_t MetadataWriter::Ptr() {
	return BasePtr() + offset;
}

}
