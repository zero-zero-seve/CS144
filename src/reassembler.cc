#include "reassembler.hh"

#include "debug.hh"

using namespace std;

void Reassembler::insert(uint64_t first_index, string data, bool is_last_substring) {
    // 计算得到first_unassembled_index以及last_unassembled_index 左闭右开区间
    uint64_t first_unassembled_index = nextIndex;
    uint64_t last_unassembled_index = first_unassembled_index + output_.writer().available_capacity();
    // 计算得到一个比较合理的first_index
    uint64_t new_first_index = first_index <= first_unassembled_index ? first_unassembled_index : first_index;
    uint64_t new_last_index = first_index + data.length() >= last_unassembled_index
                                  ? last_unassembled_index
                                  : first_index + data.length();

    setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
    if (is_last_substring && data.size() == 0 && map_.size() == 0) {
        output_.writer().close();
    }
    if (new_first_index >= new_last_index) {
        return;
    }

    // 查表，得到第一个index大于等于new_first_index的位置
    auto it1 = map_.lower_bound(new_first_index);
    if (it1 == map_.end()) {
        // setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
        if (map_.size() != 0) {
            --it1;
            if (it1->first + it1->second.length() >= new_last_index) {
                return;
            }
            if (it1->first + it1->second.length() > new_first_index) {
                new_first_index = it1->first + it1->second.length();
            }
        }
        if (new_first_index == nextIndex) {
            output_.writer().push(
                data.substr(new_first_index - first_index, new_last_index - new_first_index));
            nextIndex = new_last_index;
            if (isGetLastString && map_.size() == 0) {
                output_.writer().close();
            }
            return;
        } else {
            map_.insert(make_pair(new_first_index, data.substr(new_first_index - first_index,
                                                               new_last_index - new_first_index)));
            return;
        }
    }
    ////在得到一个最小上界后
    auto it2 = map_.upper_bound(new_first_index);
    auto lit1 = map_.lower_bound(new_last_index);

    if (it1 == map_.begin() && it2 == it1) {
        if (new_first_index == nextIndex) {
            // setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
            while (it2 != lit1) {
                if (it2->first + it2->second.length() < new_last_index) {
                    auto temp = it2;
                    ++it2;
                    map_.erase(temp);
                } else {
                    new_last_index = it2->first;
                    break;
                }
            }
            output_.writer().push(
                data.substr(new_first_index - first_index, new_last_index - new_first_index));
            nextIndex = new_last_index;
            while (it2 != map_.end() && it2->first == nextIndex) {
                output_.writer().push(it2->second);
                nextIndex = it2->first + it2->second.length();
                auto temp = it2;
                ++it2;
                map_.erase(temp);
            }
            if (isGetLastString && map_.size() == 0) {
                output_.writer().close();
            }
            return;
        } else {
            while (it2 != lit1) {
                if (it2->first + it2->second.length() < new_last_index) {
                    auto temp = it2;
                    ++it2;
                    map_.erase(temp);
                } else {
                    new_last_index = it2->first;
                    break;
                }
            }
            // setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
            map_.insert(make_pair(new_first_index, data.substr(new_first_index - first_index,
                                                               new_last_index - new_first_index)));
            return;
        }

        // if (new_first_index == nextIndex) {
        //     output_.writer().push(
        //         data.substr(new_first_index - first_index, new_last_index - new_first_index));
        //     nextIndex = new_last_index;
        //     while (it1 != map_.end() && it1->first == nextIndex) {
        //         output_.writer().push(it1->second);
        //         nextIndex = it1->first + it1->second.length();
        //         auto temp = it1;
        //         ++it1;
        //         map_.erase(temp);
        //     }
        //     if (isGetLastString && map_.size() == 0) {
        //         output_.writer().close();
        //     }
        //     return;
        // } else {
        //     map_.insert(make_pair(new_first_index, data.substr(new_first_index - first_index,
        //                                                        new_last_index - new_first_index)));
        //     return;
        // }
    }

    if (it1 != it2) {
        if (it1->first + it1->second.length() >= new_last_index) {
            return;
        } else {
            while (it1 != lit1) {
                if (it1->first + it1->second.length() < new_last_index) {
                    auto temp = it1;
                    ++it1;
                    map_.erase(temp);
                } else {
                    new_last_index = it1->first;
                    break;
                }
            }
            // setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
            map_.insert(make_pair(new_first_index, data.substr(new_first_index - first_index,
                                                               new_last_index - new_first_index)));
        }
    } else {
        --it1;
        if (it1->first + it1->second.length() >= new_last_index) {
            return;
        }
        if (it1->first + it1->second.length() > new_first_index) {
            new_first_index = it1->first + it1->second.length();
        }

        while (it2 != lit1) {
            if (it2->first + it2->second.length() < new_last_index) {
                auto temp = it2;
                ++it2;
                map_.erase(temp);
            } else {
                new_last_index = it2->first;
                break;
            }
        }
        // setIsLastStr(is_last_substring, new_last_index, first_index + data.length());
        map_.insert(make_pair(new_first_index,
                              data.substr(new_first_index - first_index, new_last_index - new_first_index)));
    }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const {
    uint64_t sum = 0;
    for (auto it = map_.begin(); it != map_.end(); ++it) {
        sum += it->second.length();
    }
    return sum;
}
