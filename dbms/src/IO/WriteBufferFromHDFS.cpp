#include <Common/config.h>

#if USE_HDFS

#include <IO/WriteBufferFromHDFS.h>
#include <IO/HDFSCommon.h>
#include <hdfs/hdfs.h>


namespace DB
{

namespace ErrorCodes
{
extern const int NETWORK_ERROR;
extern const int CANNOT_OPEN_FILE;
extern const int CANNOT_FSYNC;
}


struct WriteBufferFromHDFS::WriteBufferFromHDFSImpl
{
    std::string hdfs_uri;
    hdfsFile fout;
    HDFSBuilderPtr builder;
    HDFSFSPtr fs;

    WriteBufferFromHDFSImpl(const std::string & hdfs_name_)
        : hdfs_uri(hdfs_name_)
        , builder(createHDFSBuilder(hdfs_uri))
        , fs(createHDFSFS(builder.get()))
    {
        const size_t begin_of_path = hdfs_uri.find('/', hdfs_uri.find("//") + 2);
        const std::string path = hdfs_uri.substr(begin_of_path);
        if (path.find("*?{") != std::string::npos)
            throw Exception("URI '" + hdfs_uri + "' contains globs, so the table is in readonly mode", ErrorCodes::CANNOT_OPEN_FILE);

        fout = hdfsOpenFile(fs.get(), path.c_str(), O_WRONLY, 0, 0, 0);

        if (fout == nullptr)
        {
            throw Exception("Unable to open HDFS file: " + path + " error: " + std::string(hdfsGetLastError()),
                ErrorCodes::CANNOT_OPEN_FILE);
        }

    }

    ~WriteBufferFromHDFSImpl()
    {
        hdfsCloseFile(fs.get(), fout);
    }


    int write(const char * start, size_t size)
    {
        int bytes_written = hdfsWrite(fs.get(), fout, start, size);
        if (bytes_written < 0)
            throw Exception("Fail to write HDFS file: " + hdfs_uri + " " + std::string(hdfsGetLastError()),
                ErrorCodes::NETWORK_ERROR);
        return bytes_written;
    }

    void sync()
    {
        int result = hdfsSync(fs.get(), fout);
        if (result < 0)
            throwFromErrno("Cannot HDFS sync" + hdfs_uri + " " + std::string(hdfsGetLastError()),
                ErrorCodes::CANNOT_FSYNC);
    }
};

WriteBufferFromHDFS::WriteBufferFromHDFS(const std::string & hdfs_name_, size_t buf_size)
    : BufferWithOwnMemory<WriteBuffer>(buf_size)
    , impl(std::make_unique<WriteBufferFromHDFSImpl>(hdfs_name_))
{
}


void WriteBufferFromHDFS::nextImpl()
{
    if (!offset())
        return;

    size_t bytes_written = 0;

    while (bytes_written != offset())
        bytes_written += impl->write(working_buffer.begin() + bytes_written, offset() - bytes_written);
}


void WriteBufferFromHDFS::sync()
{
    impl->sync();
}

WriteBufferFromHDFS::~WriteBufferFromHDFS()
{
    try
    {
        next();
    }
    catch (...)
    {
        tryLogCurrentException(__PRETTY_FUNCTION__);
    }
}

}
#endif
