////////////////////////////////

#include <Windows.h>
#include <stdio.h>

#if !defined(SAME_FILE_TWICE)
#define SAME_FILE_TWICE 0
#endif

#if !defined(RENAME_FILE)
#define RENAME_FILE 0
#endif

#define ArrayCount(a) (sizeof(a)/sizeof(*(a)))
#define UnusedVariable(name) (void)name;

static void AssertMessage_do_exit_(int condition, char *message, int do_exit){
    if(!condition){
        printf("%s\n", message);

        DWORD errorMessageID = GetLastError();
        LPSTR messageBuffer = 0;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
        //printf("%s\n", messageBuffer);
        fflush(stdout);

        if(do_exit){
            ExitProcess(0);
        }
    }
}

static void AssertMessageDontExit(int condition, char *message){
    AssertMessage_do_exit_(condition, message, 0);
}

static void AssertMessage(int condition, char *message){
    AssertMessage_do_exit_(condition, message, 1);
}


void
print_hz(void){
    printf("----------------------------------------------------------------\n");
}

void
print_hz_small(void){
    printf("--------------------------------\n");
}

typedef struct{
    DWORD val;
    char *name;
} CreateFileParam;

CreateFileParam access_params[] = {
    {GENERIC_READ, "read"},
    {GENERIC_WRITE, "write"},
#if 0
    {DELETE, "delete"},
#endif
    {GENERIC_READ|GENERIC_WRITE, "read-write"},
#if 0
    {GENERIC_READ|DELETE, "read-delete"},
    {GENERIC_WRITE|DELETE, "write-delete"},
    {GENERIC_READ|GENERIC_WRITE|DELETE, "read-write-delete"},
#endif
};

CreateFileParam share_params[] = {
    {0, "none"},
    {FILE_SHARE_READ, "read"},
    {FILE_SHARE_WRITE, "write"},
    {FILE_SHARE_DELETE, "delete"},
    {FILE_SHARE_READ|FILE_SHARE_WRITE, "read-write"},
    {FILE_SHARE_READ|FILE_SHARE_DELETE, "read-delete"},
    {FILE_SHARE_WRITE|FILE_SHARE_DELETE, "write-delete"},
    {FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, "read-write-delete"},
};

typedef void (*RenameProc) (HANDLE h, wchar_t *old, wchar_t *new);

static void rename_using_MoveFileEx(HANDLE h, wchar_t *old, wchar_t *new){
    UnusedVariable(h);
    int ret = MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING);
    AssertMessage(ret != 0, "Rename failed");
}

static void rename_using_SetFileInformationByHandle(HANDLE h, wchar_t *old, wchar_t *new){
    /* NOTE(mal): This one is more convenient because it only needs a handle and a new name
       This video https://youtu.be/uhRWMGBjlO8?t=2000 recommends
       SetFileInformationByHandle with FileRenameInfo and FILE_RENAME_INFO.ReplaceIfExists == true
       and warns against MoveFileEx. I haven't been able to find documentation on the supposed drawbacks
       of MoveFileEx (copy-delete semantics if regular move fails).

       The CreateFile that returns this handle has to specify the DELETE access flag, because the deletion 
       happens through the handle itself.

       Starting on some revision of Windows 10 (1607) there's explicit support for NTFS "atomic superseeding rename":
       - https://stackoverflow.com/a/51737582

       The documentation I've found does not provide much detail:
       https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle
       https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
       - SetFileInformationByHandle with FileRenameInfoEx and FILE_RENAME_FLAG_POSIX_SEMANTICS
     */
    UnusedVariable(old);

    FILE_RENAME_INFO *file_rename_info = 0;
    DWORD file_rename_info_size = 0; {
        int new_name_size = (wcslen(new)+1) * sizeof(wchar_t);
        file_rename_info_size = sizeof(FILE_RENAME_INFO) + new_name_size;
        file_rename_info = (FILE_RENAME_INFO *) malloc(file_rename_info_size);
        memcpy(file_rename_info->FileName, new, new_name_size);
        file_rename_info->FileNameLength = new_name_size;   // NOTE(mal): Don't know if it should final null byte
        file_rename_info->ReplaceIfExists = TRUE;
        file_rename_info->RootDirectory = NULL;
    }

    int ret = SetFileInformationByHandle(h, FileRenameInfo, file_rename_info, file_rename_info_size);
    AssertMessage(ret != 0, "Rename failed");
}

static void rename_using_ReplaceFile(HANDLE h, wchar_t *old, wchar_t *new){
    /* NOTE(mal): Microsoft recommends it over MovefileTransacted
       (https://docs.microsoft.com/en-us/windows/win32/fileio/deprecation-of-txf)
       It looks like its purpose is to update the content of the destination file while preserving metadata
       I'm not including it because according to the documentation the old file is opened specifying no sharing
       flags, which makes it impossible for us to hold an open handle to it.
       Also, I believe that the destination file has to exist.
     */
    BOOL ret = ReplaceFileW(new, old, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS|REPLACEFILE_IGNORE_ACL_ERRORS, 0, 0);
    AssertMessage(ret != 0, "Rename failed");
}

static void print_handle_disk_info(HANDLE h){
    BY_HANDLE_FILE_INFORMATION file_info = {0};
    BOOL ret = GetFileInformationByHandle(h, &file_info);
    AssertMessage(ret != 0, "GetFileInformationByHandleEx failed");
    printf("volume = %lx id_high = %lx id_low = %lx\n",
           file_info.dwVolumeSerialNumber, file_info.nFileIndexHigh, file_info.nFileIndexLow);
}

typedef void (*DeleteProc) (HANDLE h, wchar_t *fname);

static void delete_using_DeleteFile(HANDLE h, wchar_t *fname){
    UnusedVariable(h);
    int ret = DeleteFileW(fname);
    AssertMessage(ret != 0, "Delete failed");
}

static void delete_using_SetFileInformationByHandle(HANDLE h, wchar_t *fname){
    UnusedVariable(fname);

    FILE_DISPOSITION_INFO file_disposition_info = {0};
    file_disposition_info.DeleteFile = 1;
    int ret = SetFileInformationByHandle(h, FileDispositionInfo, &file_disposition_info, sizeof(file_disposition_info));
    AssertMessage(ret != 0, "Delete failed");
}




int
WinMain(HINSTANCE hInstance,
        HINSTANCE hPrevInstance,
        LPSTR     lpCmdLine,
        int       nShowCmd){

    // NOTE(allen): Same File Twice Test
#if SAME_FILE_TWICE
    
    print_hz();
    printf("Same File Twice:\n");
    
    for (int f1a = 0; f1a < ArrayCount(access_params); f1a += 1){
        for (int f1s = 0; f1s < ArrayCount(share_params); f1s += 1){
            for (int f2a = 0; f2a < ArrayCount(access_params); f2a += 1){
                for (int f2s = 0; f2s < ArrayCount(share_params); f2s += 1){
                    printf("h1: access=%s; share=%s;\n", access_params[f1a].name, share_params[f1s].name);
                    HANDLE h1 = CreateFileW(L"test_data\\text_file.txt",
                                            access_params[f1a].val, share_params[f1s].val,
                                            0, OPEN_EXISTING, 0, 0);
                    
                    printf("h2: access=%s; share=%s;\n", access_params[f2a].name, share_params[f2s].name);
                    HANDLE h2 = CreateFileW(L"test_data\\text_file.txt",
                                            access_params[f2a].val, share_params[f2s].val,
                                            0, OPEN_EXISTING, 0, 0);
                    
                    if (h1 == INVALID_HANDLE_VALUE){
                        printf("h1 = INVALID_HANDLE_VALUE\n");
                    }
                    else{
                        printf("h1 = %llu\n", (unsigned long long)h1);
                        CloseHandle(h1);
                    }
                    
                    if (h2 == INVALID_HANDLE_VALUE){
                        printf("h2 = INVALID_HANDLE_VALUE\n");
                    }
                    else{
                        printf("h2 = %llu\n", (unsigned long long)h2);
                        CloseHandle(h2);
                    }
                    
                    print_hz_small();
                }
            }
        }
    }
#endif

#if RENAME_FILE
    /* NOTE(mal): There are two ways of renaming a file:
      - Using its path (MoveFileEx)
      - Using an open handle (SetFileInformationByHandle)
      The second one should probably be the prefered choice if the source for my comments under
      rename_using_SetFileInformationByHandle is to be trusted.

      On my tests (conducted on NTFS and a remote CIFS volume) both behave the same, preserving the underlying
      disk id for the files, allowing to write to renamed handles and preserving all data written before and 
      after the rename.
      Both renaming methods report the new file name after a call to GetFinalPathNameByHandle(handle) on NTFS, BUT
      both report the _OLD_ file name with the particular configuration of the CIFS volume I've used.
     */

#define OLD_NAME L"test_data\\file_A.txt"
#define NEW_NAME L"test_data\\file_B.txt"
#define CONTENTS_A "asdf"
#define CONTENTS_B "jkl"

    print_hz();
    printf("Rename test:\n");

    char contents_a[] = CONTENTS_A;
    int contents_len_a = ArrayCount(contents_a)-1;

    char contents_b[] = CONTENTS_B;
    int contents_len_b = ArrayCount(contents_b)-1;

    char contents_ab[] = CONTENTS_A CONTENTS_B;
    int contents_len_ab = ArrayCount(contents_ab)-1;

    struct{
        RenameProc proc;
        char *name;
    } rename_procs_and_names[] = {
          {rename_using_MoveFileEx,                   "MoveFileEx"},
          {rename_using_SetFileInformationByHandle,   "SetFileInformationByHandle"},
          /* {rename_using_ReplaceFile,               "ReplaceFile"}, */
    };


    for(unsigned int i_proc = 0; i_proc < ArrayCount(rename_procs_and_names); ++i_proc){
        RenameProc rename_proc = rename_procs_and_names[i_proc].proc;
        char *rename_proc_name = rename_procs_and_names[i_proc].name;

        printf("- %s:\n", rename_proc_name);

        HANDLE h = CreateFileW(OLD_NAME,
                               GENERIC_READ|GENERIC_WRITE|DELETE,
                               FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 
                               0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE){
            printf("h = INVALID_HANDLE_VALUE\n");
            return(0);
        }

        print_handle_disk_info(h);

        DWORD written = 0;

        BOOL ret = WriteFile(h, contents_a, contents_len_a, &written, 0);
        AssertMessageDontExit(ret != 0 && written == contents_len_a, "Initial WriteFile failed");

        rename_proc(h, OLD_NAME, NEW_NAME);

        wchar_t path[MAX_PATH];
        DWORD path_len = GetFinalPathNameByHandleW(h, path, MAX_PATH, FILE_NAME_OPENED);
        ret = wcscmp (path + wcslen(path) - wcslen(NEW_NAME), NEW_NAME);
        AssertMessageDontExit(ret == 0, "File does not match NEW_NAME");

        ret = WriteFile(h, contents_b, contents_len_b, &written, 0);
        AssertMessageDontExit(ret != 0 && written == contents_len_b, "Second WriteFile failed");

        CloseHandle(h);

        h = CreateFileW(NEW_NAME, GENERIC_READ,
                        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 
                        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

        if (h == INVALID_HANDLE_VALUE){
            printf("h = INVALID_HANDLE_VALUE\n");
            return(0);
        }

        print_handle_disk_info(h);

        long int size = 0; {
            DWORD high_bytes;
            DWORD read_bytes = GetFileSize(h, &high_bytes);
            size = (((long long int)high_bytes) << 32) + read_bytes;
        }

        char * buffer = (char *) malloc(size);

        DWORD bytes_read = 0;

        ret = ReadFile(h, buffer, size, &bytes_read, 0);
        AssertMessageDontExit(bytes_read == contents_len_ab && memcmp(contents_ab, buffer, bytes_read) == 0, 
                      "File content mistmatch");
        CloseHandle(h);
        printf("\n");

    }


#endif

#if DELETE_FILE
    /* NOTE(mal):
       https://docs.microsoft.com/en-us/windows/win32/fileio/closing-and-deleting-files
       Deleting a file means marking it for deletion, which will only happen after all open handles to it have been
       closed. Even after that happens there's a short period of time when the file will still be visible and will 
       block the creation of a file with the name of the deleted file.
       It is then necessary to rename the target file prior to deletion to avoid this problem.

       I know of two ways of marking a file for deletion:
       - Using its path (DeleteFile):
         https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
       - Using an open handle (SetFileInformationByHandle with FileInformationClass==FileDispositionInfo and
                               DeleteFile set to True inside the FILE_DISPOSITION_INFO parameter):
         https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle and
         https://docs.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_disposition_info
     */

#define PATH_NAME L"test_data\\file_A.txt"
#define CONTENTS_A "asdf"
#define CONTENTS_B "jkl"

    print_hz();
    printf("Delete test:\n");

    char contents_a[] = CONTENTS_A;
    int contents_len_a = ArrayCount(contents_a)-1;

    char contents_b[] = CONTENTS_B;
    int contents_len_b = ArrayCount(contents_b)-1;

    char contents_ab[] = CONTENTS_A CONTENTS_B;
    int contents_len_ab = ArrayCount(contents_ab)-1;

    struct{
        DeleteProc proc;
        char *name;
    } delete_procs_and_names[] = {
          {delete_using_DeleteFile,                   "DeleteFile"},
          {delete_using_SetFileInformationByHandle,   "SetFileInformationByHandle"},
    };


    for(unsigned int i_proc = 0; i_proc < ArrayCount(delete_procs_and_names); ++i_proc){
        DeleteProc delete_proc = delete_procs_and_names[i_proc].proc;
        char *delete_proc_name = delete_procs_and_names[i_proc].name;

        printf("- %s:\n", delete_proc_name);

        HANDLE h = CreateFileW(PATH_NAME,
                               GENERIC_READ|GENERIC_WRITE|DELETE,
                               FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 
                               0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE){
            printf("h = INVALID_HANDLE_VALUE\n");
            return(0);
        }

        DWORD written = 0;

        BOOL ret = WriteFile(h, contents_a, contents_len_a, &written, 0);
        AssertMessageDontExit(ret != 0 && written == contents_len_a, "Initial WriteFile failed");

        delete_proc(h, PATH_NAME);

        ret = WriteFile(h, contents_b, contents_len_b, &written, 0);
        AssertMessageDontExit(ret != 0 && written == contents_len_b, "Second WriteFile failed");

        // TODO: Check content
        DWORD ret_dw = SetFilePointer(h, 0, 0, FILE_BEGIN);
        AssertMessage(ret_dw != INVALID_SET_FILE_POINTER, "Seek failed");

        long int size = 0; {
            DWORD high_bytes;
            DWORD read_bytes = GetFileSize(h, &high_bytes);
            size = (((long long int)high_bytes) << 32) + read_bytes;
        }

        char * buffer = (char *) malloc(size);
        
        DWORD bytes_read = 0;
        ret = ReadFile(h, buffer, size, &bytes_read, 0);
        AssertMessageDontExit(bytes_read == contents_len_ab && memcmp(contents_ab, buffer, bytes_read) == 0, 
                      "File content mistmatch");

        CloseHandle(h);

        DWORD dwAttrib = GetFileAttributesW(PATH_NAME);

        int exists = (dwAttrib != INVALID_FILE_ATTRIBUTES && 
                      !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

        AssertMessageDontExit(!exists, "File still exists after marked for deletion");

        CloseHandle(h);
        printf("\n");

    }


#endif

    
    return(0);
}
