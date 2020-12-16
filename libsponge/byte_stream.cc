#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity):
    buffer(capacity + 1, '\0'), seq_acked(0), seq_send(0) { 
}

size_t ByteStream::write(const string &data) {
    size_t max_size = buffer.size();  // buffer实际能使用的空间为size - 1
    size_t cnt = remaining_capacity();

    if (data.size() <= cnt) {    // 有足够的空间存放数据
        cnt = data.size();
    }

    for (size_t i = 0; i < cnt; i++) {
        buffer[seq_send] = data[i];
        seq_send = (seq_send + 1) % max_size;
    }

    written_bytes += cnt;
    return cnt;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string data;
    size_t acked = seq_acked;
    size_t max_size = buffer.size();
    size_t i = 0;
    while (acked != seq_send && i < len) {
        data.push_back(buffer[acked]);
        i++;
        acked = (acked + 1) % max_size;
    }
    return data;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    size_t max_size = buffer.size();
    size_t i = 0;
    while (seq_acked != seq_send && i < len) {
        i++;
        seq_acked = (seq_acked + 1) % max_size;
    }
    read_bytes += i;
 }

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string data;
    size_t max_size = buffer.size();
    size_t i = 0;
    while (seq_acked != seq_send && i < len) {
        data.push_back(buffer[seq_acked]);
        i++;
        seq_acked = (seq_acked + 1) % max_size;
    }
    read_bytes += i;
    return data;
}

void ByteStream::end_input() {
    end = true;
}

bool ByteStream::input_ended() const { return end; }

size_t ByteStream::buffer_size() const {
    size_t max_size = buffer.size();  // buffer实际能使用的空间为size - 1
    return (seq_send + max_size - seq_acked) % max_size;
}

bool ByteStream::buffer_empty() const { return seq_send == seq_acked; }

bool ByteStream::eof() const { 
    return end && buffer_empty();
 }

size_t ByteStream::bytes_written() const { return written_bytes; }

size_t ByteStream::bytes_read() const { return read_bytes; }

size_t ByteStream::remaining_capacity() const { 
    // buffer 中有一个元素用于判空和判满，因此总的容量为size - 1
    return buffer.size() - 1 - buffer_size(); 
}
