#pragma once

#include "xr_types.hxx"
#include "xr_reader.hxx"
#include "xr_writer.hxx"

#include <string>
#include <vector>

namespace xray_re
{
	struct split_path_t
	{
		std::string folder;
		std::string name;
		std::string extension;
	};

	class xr_file_system
	{
	public:
		enum
		{
			FSF_READ_ONLY = 0x1,
		};

		xr_file_system();
		~xr_file_system();

		static xr_file_system& instance();

		bool initialize(const std::string& fs_spec, unsigned flags = 0);
		bool read_only() const;

		static xr_reader* r_open(const std::string& path);
		xr_reader* r_open(const std::string& path, const std::string& name) const;
		static void r_close(xr_reader*& r);
		xr_writer* w_open(const std::string& path, bool ignore_ro = false) const;
		xr_writer* w_open(const std::string& path, const std::string& name, bool ignore_ro = false) const;
		static void w_close(xr_writer*& w);

		bool copy_file(const std::string& src_path, const std::string& src_name, const std::string& tgt_path, const std::string& tgt_name = nullptr) const;
		bool copy_file(const std::string& src_path, const std::string& tgt_path) const;

		static size_t file_length(const std::string& path);
		static uint32_t file_age(const std::string& path);
		static bool file_exist(const std::string& path);
		static bool folder_exist(const std::string& path);
		bool create_path(const std::string& path) const;
		bool create_folder(const std::string& path) const;
		const char* resolve_path(const std::string &path) const;
		bool resolve_path(const std::string& path, const std::string& name, std::string& full_path) const;
		void update_path(const std::string& path, const std::string& root, const std::string& add);
		static void append_path_separator(std::string& path);
		static split_path_t split_path(const std::string& path);

	protected:
		struct path_alias
		{
			std::string path;
			std::string root;
			std::string filter;
			std::string caption;
		};

		const path_alias* find_path_alias(const std::string& path) const;
		path_alias* add_path_alias(const std::string& path, const std::string& root, const std::string& add);
		bool parse_fs_spec(xr_reader& reader);

		void working_folder(std::string& folder);

	private:
		std::vector<path_alias*> m_aliases;
		unsigned int m_flags;
	};

	class xr_mmap_reader_posix: public xr_reader
	{
	public:
		xr_mmap_reader_posix();
		xr_mmap_reader_posix(int fd, void *data, size_t file_length, size_t mem_lenght);
		virtual ~xr_mmap_reader_posix();

	private:
		int m_fd;
		size_t m_file_length;
		size_t m_mem_length;
	};

	class xr_file_writer_posix: public xr_writer
	{
	public:
		xr_file_writer_posix();
		explicit xr_file_writer_posix(int fd);
		virtual ~xr_file_writer_posix() override;
		virtual void w_raw(const void *data, size_t length) override;
		virtual void seek(size_t pos) override;
		virtual size_t tell() override;

	private:
		int m_fd;
	};

	static const std::string PA_FS_ROOT = "$fs_root$";
}
