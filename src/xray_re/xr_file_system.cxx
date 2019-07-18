#include "xr_file_system.hxx"
#include "xr_math.hxx"
#include "xr_utils.hxx"
#include "xr_log.hxx"
#include "xr_string_utils.hxx"
#include "xr_log.hxx"

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <iostream>

using namespace xray_re;

xr_file_system::xr_file_system(): m_flags(0) {}

xr_file_system::~xr_file_system()
{
	delete_elements(m_aliases);
}

xr_file_system& xr_file_system::instance()
{
	static xr_file_system instance0;
	return instance0;
}

bool xr_file_system::read_only() const { return !!(m_flags & FSF_READ_ONLY); }

bool xr_file_system::initialize(const std::string& fs_spec, unsigned flags)
{
	if (!fs_spec.empty() && fs_spec[0] != '\0')
	{
		xr_reader *r = r_open(fs_spec);
		if (r == nullptr)
			return false;

		if (parse_fs_spec(*r))
		{
			auto path_splitted = xr_file_system::split_path(fs_spec);

			std::string folder = path_splitted.folder;

			add_path_alias(PA_FS_ROOT, folder, "");
		}
		r_close(r);
	}
	else
	{
#if 0
		std::string folder;
		get_working_folder(folder);
#endif
		add_path_alias(PA_FS_ROOT, "", "");
	}
	m_flags = flags;
	return !m_aliases.empty();
}

xr_reader* xr_file_system::r_open(const std::string& path) const
{
	auto fd = ::open(path.c_str(), O_RDONLY);
	if (fd == -1)
	{
		dbg("Failed to open file \"%s\": %s (errno=%d) ", path.c_str(), strerror(errno), errno);
		return nullptr;
	}

	struct stat sb;
	if (fstat(fd, &sb) == -1)
	{
		dbg("stat failed for file \"%s\": %s (errno=%d) ", path.c_str(), strerror(errno), errno);
		return nullptr;
	}

	//size_t file_size = static_cast<size_t>(lseek(fd, 0, SEEK_END));
	size_t file_size = static_cast<size_t>(sb.st_size);

	xr_reader *reader = nullptr;

	size_t mem_size;
	size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	size_t remainder = file_size % page_size;

	if(remainder == 0)
	{
		mem_size = file_size;
	}
	else
	{
		mem_size = file_size + page_size - remainder;
	}

	void  *data = mmap(nullptr, mem_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);

	if(file_size != 0)
	{
		if(data == MAP_FAILED)
		{
			dbg("mmap failed for file \"%s\": %s (errno=%d) ", path.c_str(), strerror(errno), errno);
			return nullptr;
		}
	}

	reader = new xr_mmap_reader_posix(fd, data, file_size, mem_size);

	return reader ? reader : nullptr;
}

xr_reader* xr_file_system::r_open(const std::string& path, const std::string& name) const
{
	const path_alias *pa = find_path_alias(path);
	return pa ? r_open(pa->root + name) : nullptr;
}

void xr_file_system::r_close(xr_reader *&r) const { delete r; r = nullptr; }

xr_writer* xr_file_system::w_open(const std::string& path, bool ignore_ro) const
{
	if(!ignore_ro && read_only())
	{
		dbg("fs_ro: writing %s", path.c_str());
		return new xr_fake_writer();
	}

	auto fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);

	if (fd == -1)
	{
		//dbg("Failed to open file \"%s\": %s (errno=%d) ", path.c_str(), strerror(errno), errno);
		return nullptr;
	}

	xr_writer *writer = new xr_file_writer_posix(fd);
	return writer;
}

xr_writer* xr_file_system::w_open(const std::string& path, const std::string& name,  bool ignore_ro) const
{
	const path_alias *pa = find_path_alias(path);
	return pa ? w_open(pa->root + name, ignore_ro) : nullptr;
}

void xr_file_system::w_close(xr_writer *&w) const { delete w; w = nullptr; }

bool xr_file_system::copy_file(const std::string &src_path, const std::string &src_name, const std::string &tgt_path, const std::string &tgt_name) const
{
	const path_alias *src_pa = find_path_alias(src_path);
	if (src_pa == nullptr)
		return false;

	const path_alias *tgt_pa = find_path_alias(tgt_path);
	if (tgt_pa == nullptr)
		return false;

	return copy_file(src_pa->root + src_name, tgt_pa->root + (tgt_name.empty() ? src_name : tgt_name));
}

bool xr_file_system::copy_file(const std::string& src_path, const std::string& dst_path) const
{
	if(read_only())
	{
		dbg("fs_ro: copying %s to %s", src_path.c_str(), dst_path.c_str());
		return true;
	}

	return std::filesystem::copy_file(src_path, dst_path);
}

size_t xr_file_system::file_length(const std::string& path)
{
	return std::filesystem::file_size(path);
}

//size_t xr_file_system::file_length(const std::string& path, const std::string& name) const
//{
//	const path_alias *pa = find_path_alias(path);
//	return pa ? file_length(pa->root + name) : 0; // Check
//}

uint32_t xr_file_system::file_age(const std::string& path)
{
	struct stat st;
	if(stat(path.c_str(), &st) == 0)
	{
		return static_cast<uint32_t>(st.st_mtime);
	}

	return 0;
}

//uint32_t xr_file_system::file_age(const std::string& path, const std::string& name) const
//{
//	const path_alias *pa = find_path_alias(path);
//	return pa ? file_age(pa->root + name) : 0;
//}

bool xr_file_system::file_exist(const std::string& path)
{
	return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

//bool xr_file_system::file_exist(const std::string& path, const std::string& name) const
//{
//	const path_alias *pa = find_path_alias(path);
//	return pa ? file_exist(pa->root + name) : false;
//}

bool xr_file_system::folder_exist(const std::string& path)
{
	return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

//bool xr_file_system::folder_exist(const std::string& path, const std::string& name) const
//{
//	const path_alias *pa = find_path_alias(path);
//	return pa ? folder_exist(pa->root + name) : false;
//}

bool xr_file_system::create_path(const std::string& path) const
{
	if(read_only())
	{
		dbg("fs_ro: creating path %s", path.c_str());
		return true;
	}

	if(std::filesystem::exists(path))
	{
		return true;
	}

	return std::filesystem::create_directories(path);
}

bool xr_file_system::create_folder(const std::string& path) const
{
	if(read_only())
	{
		dbg("fs_ro: creating folder %s", path.c_str());
		return true;
	}

	if(std::filesystem::exists(path))
	{
		return true;
	}

	return std::filesystem::create_directory(path);
}

//bool xr_file_system::create_folder(const std::string& path, const std::string& name) const
//{
//	const path_alias *pa = find_path_alias(path);
//	if (pa == nullptr)
//		return false;

//	if (read_only())
//	{
//		dbg("fs_ro: creating folder %s%s", pa->root.c_str(), name.c_str());
//		return true;
//	}

//	return create_folder(pa->root + name);
//}

const char* xr_file_system::resolve_path(const std::string& path) const
{
	const path_alias *pa = find_path_alias(path);
	return pa != nullptr ? pa->root.c_str() : nullptr;
}

bool xr_file_system::resolve_path(const std::string& path, const std::string& name, std::string& full_path) const
{
	const path_alias *pa = find_path_alias(path);
	if (pa == nullptr)
		return false;

	full_path = pa->root;

	if (name.empty())
		full_path.append(name);

	return true;
}

void xr_file_system::update_path(const std::string& path, const std::string& root, const std::string& add)
{
	path_alias *new_pa;
	for (path_alias_vec_cit it = m_aliases.begin(), end = m_aliases.end(); it != end; ++it)
	{
		if ((*it)->path == path)
		{
			new_pa = *it;
			goto found_or_created;
		}
	}
	new_pa = new path_alias;
	new_pa->path = path;
	m_aliases.push_back(new_pa);

found_or_created:
	const path_alias *pa = find_path_alias(root);
	if (pa)
	{
		new_pa->root = pa->root;
	}
	else
	{
		new_pa->root = root;
		append_path_separator(new_pa->root);
	}
	new_pa->root += add;
	append_path_separator(new_pa->root);
}

void xr_file_system::append_path_separator(std::string& path)
{
	if (!path.empty() && *(path.end() - 1) != '/')
		path += '/';
}

split_path_t xr_file_system::split_path(const std::string& path)
{
	std::filesystem::path fs_path(path);

	return {fs_path.parent_path(), fs_path.stem(), fs_path.extension()};
}

const xr_file_system::path_alias* xr_file_system::find_path_alias(const std::string& path) const
{
	for (path_alias_vec_cit it = m_aliases.begin(), end = m_aliases.end(); it != end; ++it)
	{
		if ((*it)->path == path)
			return *it;
	}

	return nullptr;
}

xr_file_system::path_alias* xr_file_system::add_path_alias(const std::string& path, const std::string& root, const std::string& add)
{
	const path_alias *pa = find_path_alias(path.c_str());

	assert(pa == nullptr);

	if (pa != nullptr)
		return nullptr;

	path_alias *new_pa = new path_alias;
	m_aliases.push_back(new_pa);
	new_pa->path = path;

	pa = find_path_alias(root.c_str());
	if (pa)
	{
		new_pa->root = pa->root;
	}
	else
	{
		new_pa->root = root;
		append_path_separator(new_pa->root);
	}

	new_pa->root += add;
	append_path_separator(new_pa->root);
	return new_pa;
}

static inline const char* next_line(const char *p, const char *end)
{
	while (p < end && *p++ != '\n') {}
	return p;
}

static inline const char* read_alias(const char *p, const char *end)
{
	if (p >= end || *p++ != '$')
		return nullptr;

	if (p >= end || (!std::isalnum(*p) && *p != '_'))
		return nullptr;

	for (++p; p < end;)
	{
		int c = *p;
		if (c == '$')
			return p + 1;

		else if (!std::isalnum(c) && c != '_')
			break;

		++p;
	}
	return nullptr;
}

static inline const char* skip_ws(const char *p, const char *end)
{
	while (p < end)
	{
		int c = *p;

		if (c != ' ' && c != '\t')
			break;

		++p;
	}
	return p;
}

static inline const char* read_value(const char *&_p, const char *end)
{
	const char *p = skip_ws(_p, end);
	_p = p;
	const char *last_ws = nullptr;
	while (p < end)
	{
		int c = *p;
		if (c == ' ' || c =='\t')
		{
			if (last_ws == nullptr)
				last_ws = p;
		}
		else if (c == '\n' || c == '\r' || c == '|')
		{
			if (last_ws == nullptr)
				last_ws = p;

			break;
		}
		else
		{
			last_ws = nullptr;
		}
		++p;
	}
	return last_ws ? last_ws : p;
}

bool xr_file_system::parse_fs_spec(xr_reader& r)
{
	const char *p = r.pointer<const char>();
	const char *end = p + r.size();
	std::string alias, values[4];
	for (unsigned line = 1; p < end; p = next_line(p, end), ++line)
	{
		int c = *p;
		if (c == '$')

		{
			const char *last = read_alias(p, end);
			if (last == nullptr)
			{
				msg("can't parse line %u", line);
				return false;
			}
			alias.assign(p, last);

			p = skip_ws(last, end);
			if (p == end || *p++ != '=')
			{
				msg("can't parse line %u", line);
				return false;
			}

			int i;
			for (i = -2; i < 4;)
			{
				last = read_value(p, end);
				if (i < 0 && (last == end || *last != '|'))
				{
					msg("can't parse line %u", line);
					return false;
				}
				if (i >= 0)
					values[i].assign(p, last);

				p = last + 1;
				++i;

				if (p == end || *last != '|')
					break;
			}

			assert(i > 0);

			if (i < 2)
				values[1].clear();

			path_alias *pa = add_path_alias(alias, values[0], values[1]);
			if (pa == nullptr)
			{
				msg("can't parse line %u", line);
				return false;
			}

			if (i > 2)
				pa->filter = values[2];
			if (i > 3)
				pa->caption = values[3];
		}
		else if (c != ';' && !std::isspace(c))
		{
			msg("can't parse line %u", line);
			return false;
		}
	}
	return true;
}

xr_mmap_reader_posix::xr_mmap_reader_posix(): xr_reader(nullptr, 0), m_fd(-1), m_file_length(0), m_mem_length(0) {}

xr_mmap_reader_posix::xr_mmap_reader_posix(int fd, void *data, size_t file_length, size_t mem_lenght) :
    xr_reader(data, file_length), m_fd(fd), m_file_length(file_length), m_mem_length(mem_lenght) {}

xr_mmap_reader_posix::~xr_mmap_reader_posix()
{
	assert(m_fd != -1);
	assert(m_data != nullptr);

	if(m_mem_length != 0)
	{
		int res = madvise(reinterpret_cast<void*>(const_cast<uint8_t*>(m_data)), m_mem_length, MADV_DONTNEED | MADV_FREE);
		if (res == -1)
		{
			dbg("madvise failed: %s (errno=%d) ", strerror(errno), errno);
		}

		res = munmap(reinterpret_cast<void*>(const_cast<uint8_t*>(m_data)), m_mem_length);
		if(res != 0)
		{
			dbg("munmap failed with result %d: %s (errno=%d) ", res, strerror(errno), errno);
		}
	}

	int res = posix_fadvise(m_fd, 0, static_cast<off_t>(m_file_length), POSIX_FADV_DONTNEED); //POSIX_FADV_NOREUSE
	if (res != 0)
	{
		dbg("posix_fadvise failed: %s (errno=%d) ", strerror(errno), errno);
	}

	res = ::close(m_fd);
	if (res == -1)
	{
		dbg("Failed to close file descriptor %d: %s (errno=%d) ", m_fd, strerror(errno), errno);
	}
}

xr_file_writer_posix::xr_file_writer_posix(): m_fd(-1) {}

xr_file_writer_posix::xr_file_writer_posix(int fd): m_fd(fd) {}

xr_file_writer_posix::~xr_file_writer_posix()
{
	assert(m_fd != -1);

	auto res = ::close(m_fd);
	if (res == -1)
	{
		dbg("Failed to close file descriptor %d: %s (errno=%d) ", m_fd, strerror(errno), errno);
	}
}

void xr_file_writer_posix::w_raw(const void *data, size_t length)
{
	auto res = ::write(m_fd, data, length);

	if(length != 0 && data != MAP_FAILED)
	{
		if (res == -1)
		{
			dbg("Failed to write to descriptor %d: %s (errno=%d) ", m_fd, strerror(errno), errno);
		}

		xr_assert(static_cast<size_t>(res) == length);
	}
}

void xr_file_writer_posix::seek(size_t pos)
{
	auto res = lseek(m_fd, static_cast<off_t>(pos), SEEK_SET);

	xr_assert(static_cast<size_t>(res) == pos);
}

size_t xr_file_writer_posix::tell()
{
	auto res = ::lseek64(m_fd, 0, SEEK_CUR);
	if (res == -1)
	{
		dbg("Failed to close file descriptor %d: %s (errno=%d) ", m_fd, strerror(errno), errno);
	}

	return static_cast<size_t>(res);
}
