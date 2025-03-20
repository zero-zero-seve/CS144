#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity )
  : capacity_( capacity )
  , error_( false )
  , isClosed( false )
  , count_r( 0 )
  , count_w( 0 )
  , buffer( capacity + 1, '\0' )
{
  buffer.clear(); // 字符串结尾的\0字符需要额外空间
}

void Writer::push( string data )
{
  // (void)data; // Your code here.
  // 检查是否有容量
  uint64_t len = data.length();
  uint64_t cap = available_capacity();
  if ( cap < len ) {
    buffer += data.substr( 0, cap );
    count_w += cap;
  } else {
    buffer += data;
    count_w += len;
  }
}

void Writer::close()
{
  // Your code here.
  isClosed = true;
}

bool Writer::is_closed() const
{
  return isClosed; // Your code here.
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - count_w + count_r; // Your code here.
}

uint64_t Writer::bytes_pushed() const
{
  return count_w; // Your code here.
}

string_view Reader::peek() const
{
  return string_view( buffer ); // Your code here.
}

void Reader::pop( uint64_t len )
{
  if ( count_w - count_r > len ) {
    buffer.erase( 0, len );
    count_r += len;
  } else if ( count_w - count_r <= len ) {
    buffer.clear();
    count_r += count_w - count_r;
  }
  // (void)len; // Your code here.
}

bool Reader::is_finished() const
{
  return isClosed && ( count_r == count_w ); // Your code here.
}

uint64_t Reader::bytes_buffered() const
{
  return count_w - count_r; // Your code here.
}

uint64_t Reader::bytes_popped() const
{
  return count_r; // Your code here.
}
