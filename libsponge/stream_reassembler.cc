#include "stream_reassembler.hh"

#include <algorithm>
#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) :
    _output(capacity), _capacity(capacity), index_vec(), index_map() {
}

bool StreamReassembler::reassemble_substring() {
    uint64_t first{}, second{};
    uint64_t fst_size{};
    if (index_map.size() <= 1)
        return true;
    // 查找重叠部分
    for (size_t i = 0; i < index_vec.size() - 1; i++) {
        first = index_vec[i];
        second = index_vec[i + 1];
        if (first >= second) {
            std::cerr<<"first >= second： "<< first<< ","<< second<<std::endl;
        }
        fst_size = index_map[first].first.size();
        if (fst_size + first > second) {
            // // std::cerr<<"first >= second： "<< first<< ","<< second<<std::endl;
            // // 将index从index_vec中移除
            // if (first == new_index) {
            //     index_vec.erase(index_vec.begin() + i);
            // } else if (second == new_index) {
            //     index_vec.erase(index_vec.begin() + i + 1);
            // } else {
            //     std::cerr<<"get error index: "<<new_index<<std::endl;
            // }
            // index_map.erase(new_index);

            return false;
        }
    }

    return true;
}

void StreamReassembler::remove_index(uint64_t index) {
    for (size_t i = 0; i < index_vec.size() - 1; i++) {
        if (index == index_vec[i]) {
            index_vec.erase(index_vec.begin() + i);
            index_map.erase(index);
            return;
        }
    }
}

size_t StreamReassembler::move_index_to_bytestream() {
    size_t total_size{};
    while (index_vec.size() && index_vec[0] <= first_unassembled) {
        uint64_t index = index_vec[0];
        std::string data = index_map[index].first; 
        size_t sz{};
        if (index + data.size() >= first_unassembled) {
            data = data.substr(first_unassembled - index);
            // 字符可能会超过capacity的右边界，将超出的多余字符删除即可，不需要全部写进bytestream
            // while (sz != data.size()) {
            //     sz += _output.write(data.substr(sz));
            // }
            sz += _output.write(data.substr(sz));
            total_size += sz;
            // 超越边界的segment，即使发送了eof，也让其无效
            if (index_map[index].second && sz == data.size()) {
                // 遇到eof，则向bytestream发送输入结束信号
                _output.end_input();
            }
            first_unassembled += sz;
        }
        // std::cout<<"first_unassembled :"<< first_unassembled<<std::endl;
        index_map.erase(index);
        index_vec.erase(index_vec.begin());
    }
    return total_size;
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    
    uint64_t first_unacceptable = first_unassembled + _output.remaining_capacity();

    // index必须在有效的范围之内，也就是[first_unassembled - _output.buffer_size(), first_unacceptable)
    // if (index >= first_unacceptable || index < (first_unassembled - _output.buffer_size()))
    //     return;

    // 从测试用例可以看出，index可以落在first_unread左侧，只要index + size落在first_unassembled右侧即可
    if (index >= first_unacceptable || index + data.size() < first_unassembled) {
        return;
    }
    
    // 从测试用例可以看出，包的长度是可以超过first_unacceptable的
    // // index + size 不能大于first_unacceptable这个边界
    // if (index + data.size() > first_unacceptable)
    //     return;
    
    // 这个index开始的数据包已经存在了,将更长的数据包保留下来
    std::unordered_map<uint64_t, std::pair<std::string, bool>>::iterator it_map = index_map.find(index);
    if (it_map != index_map.end()) {
        // 选择更长的数据包；
        // 例外情况是如果小数据包的eof设置了，则选小数据包
        if ((it_map->second.first.size() < data.size() && it_map->second.second != true) ||
            (it_map->second.first.size() >= data.size() && eof)) {
            index_map[index] = std::make_pair(data, eof);
        } 
        // 因为index相同，所以该index不能是segment的开头部分，直接返回
        return;
    }
    index_map[index] = std::make_pair(data, eof);
    
    index_vec.emplace_back(index);
    std::sort(index_vec.begin(), index_vec.end());

    // size_t total_moved = move_index_to_bytestream();
    move_index_to_bytestream();
    
    
    // if (reassemble_substring()) {
    //     // 添加index成功
    //     if (index == index_vec[0]) {
    //         // size_t total_moved = move_index_to_bytestream();
    //         move_index_to_bytestream();
    //     }
    // } else {    
    //     // index导致重叠，删除这个index
    //     remove_index(index);
    // }

}

size_t StreamReassembler::unassembled_bytes() const { 
    std::unordered_map<uint64_t, std::pair<std::string, bool>>::const_iterator it_map = index_map.begin();
    size_t bytes_size{};
    while(it_map != index_map.end()) {
        bytes_size += it_map->second.first.size();
        it_map++;
    }
    uint64_t first{}, second{};
    uint64_t fst_end{}, sec_end{};
    for (size_t i = 0; i + 1 < index_vec.size(); i++) {
        first = index_vec[i];
        second = index_vec[i + 1];
        it_map = index_map.find(first);
        if (it_map != index_map.end()) {
            fst_end = first + it_map->second.first.size();
        } else {
            std::cerr<<"can't find first index: "<<first<<"i = "<<i<<std::endl;
        }
        it_map = index_map.find(second);
        if (it_map != index_map.end()) {
            sec_end = second + it_map->second.first.size();
        } else {
            std::cerr<<"can't find second index: "<<second<<"i = "<<i<<std::endl;
        }
        // 存在重叠
        if (fst_end > second) {
            if (fst_end >= sec_end) {
                bytes_size -= (sec_end - second);
            } else {
                bytes_size -= (fst_end - second);
            }
        }
    }
    return bytes_size;
 }

bool StreamReassembler::empty() const { return !unassembled_bytes(); }
