﻿// _____________________________________________________________________________________________________________________________________________
// FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR
// _____________________________________________________________________________________________________________________________________________
// FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR
// _____________________________________________________________________________________________________________________________________________
// FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR...FILE.DESCRIPTOR

#include <iostream>
#include <iomanip>
#include "fd.h"
#include "traversal.h"
using std::ostream;
using std::ios_base;
using std::setfill;
using std::setw;
using std::left;
using std::right;
using std::hex;


// check if the full filename is valid according to the file descriptor specification
MFS FileDescriptor::isFullNameValid(const char* str)
{
    // return if the given string is nullptr
    if( !str ) return MFS_BADARGS;

    // find the length of the filename and the extension
    // examples:
    // +  filename.ext❒   ->   [filename][ext]
    //    0      7 9  12         0      7  0 2
    // +  f1.e❒           ->   [f1❒-----][e❒-]
    //    01 34                  01      7  01 
    // +  a.b.c.d.eef❒    ->   [a.b.c.d❒][eef]
    //    0     6 8  11          0     6    0 2
    uns32 n = 0;           // length of input string without '\0'
    uns32 fname_len = 0;   // length of filename     without '\0'
    uns32 fext_len  = 0;   // length of extension    without '\0'
    for( ; str[n] != '\0' && n < FullFileNameSize-1 + 1; n++ )   // n goes to FullFileNameSize on purpose (not to FullFileNameSize-1), so that we can detect if the full filename is too long
    {
        // if the filename contains any of the special characters, return that it is invalid
        for( idx32 i = 0; i < special_char_cnt; i++ )
            if( str[n] == special_char[i] )
                return MFS_NOK;

        // always increase extension length (pretend that the file is only the extension bit)
        fext_len++;

        // if the '.' (dot) is found:
        // +   then the characters that we thought belonged to the extension are part of the filename
        //     and there must be more characters that are part of the extension
        // +   therefore increase filename length by the extension length, and reset the extension length
        if( str[n] == '.' )
        {
            fname_len += fext_len;
            fext_len = 0;
        }
    }

    // if the filename is not empty, remove the last dot from it
    if( fname_len > 0 ) fname_len--;

    // the filename is invalid if:
    // +   the filename length is too long   -or-
    // +   the extension length is too long  -or-
    // +   the filename and extension lengths are both zero
    if( fname_len   > FileNameSize-1
       || fext_len  > FileExtSize-1
       || fname_len + fext_len == 0 )
        return MFS_NOK;

    // return that the filename is valid
    return MFS_OK;
}



// get the depth of the file structure given the file size
MFS FileDescriptor::getDepth(siz32 filesize, siz32& depth)
{
    // if the file size is larger than the max allowed file size, return an error code
    if( filesize > FileSizeL ) return MFS_BADARGS;

    // the file's depth is exactly the number of thresholds it is greater than (for each threshold, increase the resulting depth by one)
    depth = (filesize > FileSizeT) + (filesize > FileSizeS) + (filesize > FileSizeM);

    // return that the operation was successful
    return MFS_OK;
}

// get the number of blocks on all the the file structure levels given the file size
MFS FileDescriptor::getBlockCount(siz32 filesize, siz32& indx1_cnt, siz32& indx2_cnt, siz32& data_cnt)
{
    // if the file size is larger than the max allowed file size, return an error code
    if( filesize > FileSizeL ) return MFS_BADARGS;

    // decrease the file size by the first couple of bytes that fit in the file descriptor (these bytes aren't in a data block), if the resulting value is negative, make it zero
    data_cnt = (filesize > FileSizeT) ? filesize - FileSizeT : 0;
    // divide by the data block size (in bytes) and round the resulting value up (to the ceiling)
    data_cnt = (data_cnt + DataBlkSize-1) / DataBlkSize;

    // divide the number of data blocks by the index2 block size and round the resulting value up (to the ceiling)
    indx2_cnt = (data_cnt > 1) ? (data_cnt + IndxBlkSize-1) / IndxBlkSize : 0;

    // divide the number of index2 blocks by the index1 block size and round the resulting value up (to the ceiling)
    indx1_cnt = (indx2_cnt > 1) ? (indx2_cnt + IndxBlkSize-1) / IndxBlkSize : 0;

    // return that the operation was successful
    return MFS_OK;
}

// get the number of blocks to be allocated/freed when changing from the current file size to the next file size
MFS FileDescriptor::getResizeBlockDelta(siz32 filesize_curr, siz32 filesize_next, int32& indx1_delta, int32& indx2_delta, int32& data_delta)
{
    // if the current or next file size is larger than the max allowed file size, return an error code
    if( filesize_curr > FileSizeL || filesize_next > FileSizeL ) return MFS_BADARGS;

    // create variables that will hold the block count per level for the current file and the next file
    siz32 indx1_cnt_curr, indx2_cnt_curr, data_cnt_curr;
    siz32 indx1_cnt_next, indx2_cnt_next, data_cnt_next;

    // get the block count per level for the current filesize and the next filesize
    getBlockCount(filesize_curr, indx1_cnt_curr, indx2_cnt_curr, data_cnt_curr);
    getBlockCount(filesize_next, indx1_cnt_next, indx2_cnt_next, data_cnt_next);

    // calculate the difference
    indx1_delta = indx1_cnt_next - indx1_cnt_curr;
    indx2_delta = indx2_cnt_next - indx2_cnt_curr;
     data_delta =  data_cnt_next -  data_cnt_curr;

    // return that the operation was successful
    return MFS_OK;
}



// get the entries in the traversal path pointing to the given position in the file
MFS FileDescriptor::getTraversalEntries(siz32 pos, Traversal& f)
{
    // if the given position is outside the largest file size, return an error code
    if( pos >= FileSizeL ) return f.status = MFS_BADARGS;

    // create variables that will hold the block count per level for the given file position
    siz32 indx1_cnt, indx2_cnt, data_cnt;

    // get the block count per level for the given file size (the byte at the given position is considered to be a part of the file, that is why the file size is pos + 1)
    getBlockCount(pos + 1, indx1_cnt, indx2_cnt, data_cnt);

    // set the entries' indexes in the traversal path -- if the block count for a particular level is less or equal to one, that means that the previous level doesn't exist -- the entry index for the previous level should be invalid
    f.ent[iINDX1] = ( indx2_cnt > 1    ) ?     (indx2_cnt-1) % IndxBlkSize : nullidx32;
    f.ent[iINDX2] = (  data_cnt > 1    ) ?      (data_cnt-1) % IndxBlkSize : nullidx32;
    f.ent[iBLOCK] = ( pos >= FileSizeT ) ? (pos - FileSizeT) % DataBlkSize : nullidx32;

    // return that the operation was successful
    return f.status = MFS_OK;
}

// get the next data block start position from the given file position
MFS FileDescriptor::getNextDataBlockStartPos(siz32& pos)
{
    // if the given position is inside the last possible data block in a file or greater, return an error code
    if( pos >= FileSizeL - DataBlkSize ) return MFS_BADARGS;

    // if the position is inside the tiny file size, then there were no data blocks to begin with (the entire file up to the given position fits in the file descriptor), return the first data block position
    if( pos < FileSizeT ) { pos = FileSizeT; return MFS_OK; }

    // temporarily remove the tiny file size offset from the position
    pos -= FileSizeT;
    // make the position point to the start of the current data block
    pos -= pos % DataBlkSize;
    // increase the position by the tiny file size offset and the data block size
    pos += FileSizeT + DataBlkSize;

    // return that the operation was successful
    return MFS_OK;
}



// copy filename and extension into given char buffer (minimal length of buffer is FullFileNameSize)
MFS FileDescriptor::getFullName(char* buf) const
{
    // return if the given char buffer pointer is nullptr
    if( !buf ) return MFS_BADARGS;

    // variable which holds the full filename length
    uns32 len = 0;

    // copy filename (without extension) into buffer
    for( uns32 i = 0; fname[i] != '\0' && i < FileNameSize-1; i++ )
        buf[len++] = fname[i];

    // insert a dot character ('.') into buffer (splits filename and extension)
    buf[len++] = '.';

    // copy extension into buffer
    for( uns32 i = 0; fext[i] != '\0' && i < FileExtSize-1; i++ )
        buf[len++] = fext[i];

    // append a null character to the end of the char buffer
    buf[len++] = '\0';
    return MFS_OK;
}

// compare filename and extension with given string (null terminated)
MFS FileDescriptor::cmpFullName(const char* str) const
{
    // return if the given string is nullptr
    if( !str ) return MFS_BADARGS;

    // get the full file name from descriptor
    char full_fname[FullFileNameSize];
    getFullName(full_fname);

    // compare strings character by character
    int res = strcmp(full_fname, str);

    // return the comparison result
    if( res > 0 ) return MFS_GREATER;
    if( res < 0 ) return MFS_LESS;
                  return MFS_EQUAL;
}

// set filename and extension from given string (null terminated)
MFS FileDescriptor::setFullName(const char* str)
{
    // return if the given string is nullptr
    if( !str ) return MFS_BADARGS;

    // find the length of the filename and the extension
    // examples:
    // +  filename.ext❒   ->   [filename][ext]
    //    0      7 9  12         0      7  0 2
    // +  f1.e❒           ->   [f1❒-----][e❒-]
    //    01 34                  01      7  01 
    // +  a.b.c.d.eef❒    ->   [a.b.c.d❒][eef]
    //    0     6 8  11          0     6    0 2
    uns32 n = 0;           // length of input string without '\0'
    uns32 fname_len = 0;   // length of filename     without '\0'
    uns32 fext_len  = 0;   // length of extension    without '\0'
    for( ; str[n] != '\0' && n < FullFileNameSize-1 + 1; n++ )   // n goes to FullFileNameSize on purpose (not to FullFileNameSize-1), so that we can detect if the full filename is too long
    {
        // always increase extension length (pretend that the file is only the extension bit)
        fext_len++;

        // if the '.' (dot) is found:
        // +   then the characters that we thought belonged to the extension are part of the filename
        //     and there must be more characters that are part of the extension
        // +   therefore increase filename length by the extension length, and reset the extension length
        if( str[n] == '.' )
        {
            fname_len += fext_len;
            fext_len = 0;
        }
    }

    // if the filename is not empty, remove the last dot from it
    if( fname_len > 0 ) fname_len--;

    // the filename is invalid if:
    // +   the filename length is too long   -or-
    // +   the extension length is too long  -or-
    // +   the filename and extension lengths are both zero
    if( fname_len > FileNameSize-1
     || fext_len  > FileExtSize-1
     || fname_len + fext_len == 0 )
        return MFS_BADARGS;

    // clear the descriptor fields that hold the full filename ([filename][ext])
    resetFullName();
    uns32 from = 0, to = 0;

    // copy the filename to the file descriptor
    while( to < fname_len ) fname[to++] = str[from++];
    // append the null character to the filename if it is shorter than 8B
    if( to < FileNameSize-1 ) fname[to] = '\0';


    // skip the dot that separates the filename and extension
    from++; to = 0;


    // copy the extension to the file descriptor
    while( to < fext_len ) fext[to++] = str[from++];
    // append the null character to the extension if it is shorter than 3B
    if( to < FileExtSize-1 ) fext[to] = '\0';


    // return ok status code
    return MFS_OK;
}

// reset the file name fields to their defaults
void FileDescriptor::resetFullName()
{
    // clear the descriptor fields that hold the full filename       // [filename][ext]
    for( uns32 i = 0; i < FileNameSize - 1; i++ ) fname[i] = '\0';   //  <|        |
    for( uns32 i = 0; i < FileExtSize  - 1; i++ ) fext [i] = '\0';   //           <|
}



// reserve the file descriptor (initialize its fields)
void FileDescriptor::reserve(const char* fname)
{
    release();            // reset the file descriptor
    setFullName(fname);   // set the full filename
}

// free the file descriptor (reset file descriptor fields to their defaults)
void FileDescriptor::release()
{
    resetFullName();       // clear the descriptor fields that hold the full filename
    locINDEX = nullblk;    // resetting multi purpose index of file to an invalid number
    filesize = 0;          // resetting filesize to zero
    eos = '\0';            // reset the unused byte to zero

    // resetting first couple of bytes of the file
    for( uns32 i = 0; i < FileSizeT; i++ ) byte[i] = 0;
}



// check if the file descriptor is free
bool FileDescriptor::isFree() const { return (fname[0] == '\0' && fext[0] == '\0'); }
// check if the file descriptor is taken
bool FileDescriptor::isTaken() const { return (fname[0] != '\0' || fext[0] != '\0'); }



// get the depth of the file structure
siz32 FileDescriptor::getDepth() const { siz32 depth; getDepth(filesize, depth); return depth; }
// get the number of blocks on all the the file structure levels
void FileDescriptor::getBlockCount(siz32& indx1_cnt, siz32& indx2_cnt, siz32& data_cnt) const { getBlockCount(filesize, indx1_cnt, indx2_cnt, data_cnt); }
// get the number of blocks to be allocated/freed when changing from the current file size to the next file size
MFS FileDescriptor::getResizeBlockDelta(siz32 filesize_next, int32& indx1_delta, int32& indx2_delta, int32& data_delta) const { return getResizeBlockDelta(filesize, filesize_next, indx1_delta, indx2_delta, data_delta); }



// print file descriptor to output stream
ostream& operator<<(ostream& os, const FileDescriptor& fd)
{
    // backup format flags and set fill character
    ios_base::fmtflags f(os.flags());
    char c = os.fill('\0');

    // create a buffer that will hold full filename, and fill it with the full filename
    char fullname[FullFileNameSize];
    fd.getFullName(fullname);

    // print the file descriptor fields
    os << setfill('-')
        << setw(FullFileNameSize-1) << left << fullname << right << ":fn "
        << hex
        << setw(uns32sz*bcw) << fd.filesize << ":B "
        << setw(uns32sz*bcw) << fd.locINDEX     << ":i1   ";

    // print the first couple of bytes of the file in the file descriptor
    os << setfill('0') << hex;
    for( uns32 i = 1; i <= FileSizeS; i++ )
    {
        os << setw(uns8sz*bcw) << (uns32) fd.byte[i-1];
        if( i % 8 == 0 );
        else if( i % 2 == 0 ) os << ' ';
    }
    os << ":d";

    // restore format flags and fill character
    os.flags(f); os.fill(c);
    return os;
}



