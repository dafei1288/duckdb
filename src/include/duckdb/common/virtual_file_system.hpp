//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/virtual_file_system.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/file_system.hpp"

namespace duckdb {

// bunch of wrappers to allow registering protocol handlers
class VirtualFileSystem : public FileSystem {
public:
	VirtualFileSystem();

	unique_ptr<FileHandle> OpenFile(const string &path, uint8_t flags, FileLockType lock = FileLockType::NO_LOCK,
	                                FileCompressionType compression = FileCompressionType::UNCOMPRESSED) override;

	virtual void Read(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) override {
		handle.file_system.Read(handle, buffer, nr_bytes, location);
	};

	virtual void Write(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) override {
		handle.file_system.Write(handle, buffer, nr_bytes, location);
	}

	int64_t Read(FileHandle &handle, void *buffer, int64_t nr_bytes) override {
		return handle.file_system.Read(handle, buffer, nr_bytes);
	}

	int64_t Write(FileHandle &handle, void *buffer, int64_t nr_bytes) override {
		return handle.file_system.Write(handle, buffer, nr_bytes);
	}

	int64_t GetFileSize(FileHandle &handle) override {
		return handle.file_system.GetFileSize(handle);
	}
	time_t GetLastModifiedTime(FileHandle &handle) override {
		return handle.file_system.GetLastModifiedTime(handle);
	}
	FileType GetFileType(FileHandle &handle) override {
		return handle.file_system.GetFileType(handle);
	}

	void Truncate(FileHandle &handle, int64_t new_size) override {
		handle.file_system.Truncate(handle, new_size);
	}

	void FileSync(FileHandle &handle) override {
		handle.file_system.FileSync(handle);
	}

	// need to look up correct fs for this
	bool DirectoryExists(const string &directory) override {
		return FindFileSystem(directory)->DirectoryExists(directory);
	}
	void CreateDirectory(const string &directory) override {
		FindFileSystem(directory)->CreateDirectory(directory);
	}

	void RemoveDirectory(const string &directory) override {
		FindFileSystem(directory)->RemoveDirectory(directory);
	}

	bool ListFiles(const string &directory, const std::function<void(string, bool)> &callback) override {
		return FindFileSystem(directory)->ListFiles(directory, callback);
	}

	void MoveFile(const string &source, const string &target) override {
		FindFileSystem(source)->MoveFile(source, target);
	}

	bool FileExists(const string &filename) override {
		return FindFileSystem(filename)->FileExists(filename);
	}

	virtual void RemoveFile(const string &filename) override {
		FindFileSystem(filename)->RemoveFile(filename);
	}

	vector<string> Glob(const string &path) override {
		return FindFileSystem(path)->Glob(path);
	}

	// these goes to the default fs
	void SetWorkingDirectory(const string &path) override {
		default_fs->SetWorkingDirectory(path);
	}

	string GetWorkingDirectory() override {
		return default_fs->GetWorkingDirectory();
	}

	string GetHomeDirectory() override {
		return default_fs->GetWorkingDirectory();
	}

	idx_t GetAvailableMemory() override {
		return default_fs->GetAvailableMemory();
	}

	void RegisterSubSystem(unique_ptr<FileSystem> fs) override {
		sub_systems.push_back(move(fs));
	}

private:
	FileSystem *FindFileSystem(const string &path) {
		for (auto &sub_system : sub_systems) {
			if (sub_system->CanHandleFile(path)) {
				return sub_system.get();
			}
		}
		return default_fs.get();
	}

private:
	vector<unique_ptr<FileSystem>> sub_systems;
	const unique_ptr<FileSystem> default_fs;
};

} // namespace duckdb
