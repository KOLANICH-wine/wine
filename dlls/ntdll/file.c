/*
 * Copyright 1999, 2000 Juergen Schmied
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_LINUX_MAJOR_H
# include <linux/major.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#elif defined(MAJOR_IN_SYSMACROS)
# include <sys/sysmacros.h>
#endif
#ifdef HAVE_UTIME_H
# include <utime.h>
#endif
#ifdef HAVE_SYS_VFS_H
/* Work around a conflict with Solaris' system list defined in sys/list.h. */
#define list SYSLIST
#define list_next SYSLIST_NEXT
#define list_prev SYSLIST_PREV
#define list_head SYSLIST_HEAD
#define list_tail SYSLIST_TAIL
#define list_move_tail SYSLIST_MOVE_TAIL
#define list_remove SYSLIST_REMOVE
# include <sys/vfs.h>
#undef list
#undef list_next
#undef list_prev
#undef list_head
#undef list_tail
#undef list_move_tail
#undef list_remove
#endif
#ifdef HAVE_SYS_MOUNT_H
# include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATFS_H
# include <sys/statfs.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_VALGRIND_MEMCHECK_H
# include <valgrind/memcheck.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define NONAMELESSUNION
#include "wine/debug.h"
#include "wine/server.h"
#include "ntdll_misc.h"

#include "winternl.h"
#include "winioctl.h"
#include "ddk/ntddk.h"
#include "ddk/ntddser.h"
#define WINE_MOUNTMGR_EXTENSIONS
#include "ddk/mountmgr.h"

WINE_DEFAULT_DEBUG_CHANNEL(ntdll);

#define FILE_WRITE_TO_END_OF_FILE      ((LONGLONG)-1)
#define FILE_USE_FILE_POINTER_POSITION ((LONGLONG)-2)


/**************************************************************************
 *                 NtOpenFile				[NTDLL.@]
 *                 ZwOpenFile				[NTDLL.@]
 *
 * Open a file.
 *
 * PARAMS
 *  handle    [O] Variable that receives the file handle on return
 *  access    [I] Access desired by the caller to the file
 *  attr      [I] Structure describing the file to be opened
 *  io        [O] Receives details about the result of the operation
 *  sharing   [I] Type of shared access the caller requires
 *  options   [I] Options for the file open
 *
 * RETURNS
 *  Success: 0. FileHandle and IoStatusBlock are updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtOpenFile( PHANDLE handle, ACCESS_MASK access,
                            POBJECT_ATTRIBUTES attr, PIO_STATUS_BLOCK io,
                            ULONG sharing, ULONG options )
{
    return unix_funcs->NtOpenFile( handle, access, attr, io, sharing, options );
}

/**************************************************************************
 *		NtCreateFile				[NTDLL.@]
 *		ZwCreateFile				[NTDLL.@]
 *
 * Either create a new file or directory, or open an existing file, device,
 * directory or volume.
 *
 * PARAMS
 *	handle       [O] Points to a variable which receives the file handle on return
 *	access       [I] Desired access to the file
 *	attr         [I] Structure describing the file
 *	io           [O] Receives information about the operation on return
 *	alloc_size   [I] Initial size of the file in bytes
 *	attributes   [I] Attributes to create the file with
 *	sharing      [I] Type of shared access the caller would like to the file
 *	disposition  [I] Specifies what to do, depending on whether the file already exists
 *	options      [I] Options for creating a new file
 *	ea_buffer    [I] Pointer to an extended attributes buffer
 *	ea_length    [I] Length of ea_buffer
 *
 * RETURNS
 *  Success: 0. handle and io are updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtCreateFile( PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attr,
                              PIO_STATUS_BLOCK io, PLARGE_INTEGER alloc_size,
                              ULONG attributes, ULONG sharing, ULONG disposition,
                              ULONG options, PVOID ea_buffer, ULONG ea_length )
{
    return unix_funcs->NtCreateFile( handle, access, attr, io, alloc_size, attributes,
                                     sharing, disposition, options, ea_buffer, ea_length );
}

/***********************************************************************
 *                  Asynchronous file I/O                              *
 */

typedef NTSTATUS async_callback_t( void *user, IO_STATUS_BLOCK *io, NTSTATUS status );

struct async_fileio
{
    async_callback_t    *callback; /* must be the first field */
    struct async_fileio *next;
    HANDLE               handle;
};

struct async_fileio_read
{
    struct async_fileio io;
    char*               buffer;
    unsigned int        already;
    unsigned int        count;
    BOOL                avail_mode;
};

struct async_fileio_write
{
    struct async_fileio io;
    const char         *buffer;
    unsigned int        already;
    unsigned int        count;
};

struct async_irp
{
    struct async_fileio io;
    void               *buffer;   /* buffer for output */
    ULONG               size;     /* size of buffer */
};

static struct async_fileio *fileio_freelist;

static void release_fileio( struct async_fileio *io )
{
    for (;;)
    {
        struct async_fileio *next = fileio_freelist;
        io->next = next;
        if (InterlockedCompareExchangePointer( (void **)&fileio_freelist, io, next ) == next) return;
    }
}

static struct async_fileio *alloc_fileio( DWORD size, async_callback_t callback, HANDLE handle )
{
    /* first free remaining previous fileinfos */

    struct async_fileio *io = InterlockedExchangePointer( (void **)&fileio_freelist, NULL );

    while (io)
    {
        struct async_fileio *next = io->next;
        RtlFreeHeap( GetProcessHeap(), 0, io );
        io = next;
    }

    if ((io = RtlAllocateHeap( GetProcessHeap(), 0, size )))
    {
        io->callback = callback;
        io->handle   = handle;
    }
    return io;
}

static async_data_t server_async( HANDLE handle, struct async_fileio *user, HANDLE event,
                                  PIO_APC_ROUTINE apc, void *apc_context, IO_STATUS_BLOCK *io )
{
    async_data_t async;
    async.handle      = wine_server_obj_handle( handle );
    async.user        = wine_server_client_ptr( user );
    async.iosb        = wine_server_client_ptr( io );
    async.event       = wine_server_obj_handle( event );
    async.apc         = wine_server_client_ptr( apc );
    async.apc_context = wine_server_client_ptr( apc_context );
    return async;
}

static NTSTATUS wait_async( HANDLE handle, BOOL alertable, IO_STATUS_BLOCK *io )
{
    if (NtWaitForSingleObject( handle, alertable, NULL )) return STATUS_PENDING;
    return io->u.Status;
}

/* callback for irp async I/O completion */
static NTSTATUS irp_completion( void *user, IO_STATUS_BLOCK *io, NTSTATUS status )
{
    struct async_irp *async = user;
    ULONG information = 0;

    if (status == STATUS_ALERTED)
    {
        SERVER_START_REQ( get_async_result )
        {
            req->user_arg = wine_server_client_ptr( async );
            wine_server_set_reply( req, async->buffer, async->size );
            status = unix_funcs->virtual_locked_server_call( req );
            information = reply->size;
        }
        SERVER_END_REQ;
    }
    if (status != STATUS_PENDING)
    {
        io->u.Status = status;
        io->Information = information;
        release_fileio( &async->io );
    }
    return status;
}

/***********************************************************************
 *           FILE_GetNtStatus(void)
 *
 * Retrieve the Nt Status code from errno.
 * Try to be consistent with FILE_SetDosError().
 */
NTSTATUS FILE_GetNtStatus(void)
{
    int err = errno;

    TRACE( "errno = %d\n", errno );
    switch (err)
    {
    case EAGAIN:    return STATUS_SHARING_VIOLATION;
    case EBADF:     return STATUS_INVALID_HANDLE;
    case EBUSY:     return STATUS_DEVICE_BUSY;
    case ENOSPC:    return STATUS_DISK_FULL;
    case EPERM:
    case EROFS:
    case EACCES:    return STATUS_ACCESS_DENIED;
    case ENOTDIR:   return STATUS_OBJECT_PATH_NOT_FOUND;
    case ENOENT:    return STATUS_OBJECT_NAME_NOT_FOUND;
    case EISDIR:    return STATUS_FILE_IS_A_DIRECTORY;
    case EMFILE:
    case ENFILE:    return STATUS_TOO_MANY_OPENED_FILES;
    case EINVAL:    return STATUS_INVALID_PARAMETER;
    case ENOTEMPTY: return STATUS_DIRECTORY_NOT_EMPTY;
    case EPIPE:     return STATUS_PIPE_DISCONNECTED;
    case EIO:       return STATUS_DEVICE_NOT_READY;
#ifdef ENOMEDIUM
    case ENOMEDIUM: return STATUS_NO_MEDIA_IN_DEVICE;
#endif
    case ENXIO:     return STATUS_NO_SUCH_DEVICE;
    case ENOTTY:
    case EOPNOTSUPP:return STATUS_NOT_SUPPORTED;
    case ECONNRESET:return STATUS_PIPE_DISCONNECTED;
    case EFAULT:    return STATUS_ACCESS_VIOLATION;
    case ESPIPE:    return STATUS_ILLEGAL_FUNCTION;
    case ELOOP:     return STATUS_REPARSE_POINT_NOT_RESOLVED;
#ifdef ETIME /* Missing on FreeBSD */
    case ETIME:     return STATUS_IO_TIMEOUT;
#endif
    case ENOEXEC:   /* ?? */
    case EEXIST:    /* ?? */
    default:
        FIXME( "Converting errno %d to STATUS_UNSUCCESSFUL\n", err );
        return STATUS_UNSUCCESSFUL;
    }
}


/******************************************************************************
 *  NtReadFile					[NTDLL.@]
 *  ZwReadFile					[NTDLL.@]
 *
 * Read from an open file handle.
 *
 * PARAMS
 *  FileHandle    [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  Event         [I] Event to signal upon completion (or NULL)
 *  ApcRoutine    [I] Callback to call upon completion (or NULL)
 *  ApcContext    [I] Context for ApcRoutine (or NULL)
 *  IoStatusBlock [O] Receives information about the operation on return
 *  Buffer        [O] Destination for the data read
 *  Length        [I] Size of Buffer
 *  ByteOffset    [O] Destination for the new file pointer position (or NULL)
 *  Key           [O] Function unknown (may be NULL)
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated, and the Information member contains
 *           The number of bytes read.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtReadFile(HANDLE hFile, HANDLE hEvent,
                           PIO_APC_ROUTINE apc, void* apc_user,
                           PIO_STATUS_BLOCK io_status, void* buffer, ULONG length,
                           PLARGE_INTEGER offset, PULONG key)
{
    return unix_funcs->NtReadFile( hFile, hEvent, apc, apc_user, io_status, buffer, length, offset, key );
}


/******************************************************************************
 *  NtReadFileScatter   [NTDLL.@]
 *  ZwReadFileScatter   [NTDLL.@]
 */
NTSTATUS WINAPI NtReadFileScatter( HANDLE file, HANDLE event, PIO_APC_ROUTINE apc, void *apc_user,
                                   PIO_STATUS_BLOCK io_status, FILE_SEGMENT_ELEMENT *segments,
                                   ULONG length, PLARGE_INTEGER offset, PULONG key )
{
    return unix_funcs->NtReadFileScatter( file, event, apc, apc_user, io_status,
                                          segments, length, offset, key );
}



/******************************************************************************
 *  NtWriteFile					[NTDLL.@]
 *  ZwWriteFile					[NTDLL.@]
 *
 * Write to an open file handle.
 *
 * PARAMS
 *  FileHandle    [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  Event         [I] Event to signal upon completion (or NULL)
 *  ApcRoutine    [I] Callback to call upon completion (or NULL)
 *  ApcContext    [I] Context for ApcRoutine (or NULL)
 *  IoStatusBlock [O] Receives information about the operation on return
 *  Buffer        [I] Source for the data to write
 *  Length        [I] Size of Buffer
 *  ByteOffset    [O] Destination for the new file pointer position (or NULL)
 *  Key           [O] Function unknown (may be NULL)
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated, and the Information member contains
 *           The number of bytes written.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtWriteFile(HANDLE hFile, HANDLE hEvent,
                            PIO_APC_ROUTINE apc, void* apc_user,
                            PIO_STATUS_BLOCK io_status, 
                            const void* buffer, ULONG length,
                            PLARGE_INTEGER offset, PULONG key)
{
    return unix_funcs->NtWriteFile( hFile, hEvent, apc, apc_user, io_status, buffer, length, offset, key );
}


/******************************************************************************
 *  NtWriteFileGather   [NTDLL.@]
 *  ZwWriteFileGather   [NTDLL.@]
 */
NTSTATUS WINAPI NtWriteFileGather( HANDLE file, HANDLE event, PIO_APC_ROUTINE apc, void *apc_user,
                                   PIO_STATUS_BLOCK io_status, FILE_SEGMENT_ELEMENT *segments,
                                   ULONG length, PLARGE_INTEGER offset, PULONG key )
{
    return unix_funcs->NtWriteFileGather( file, event, apc, apc_user, io_status,
                                          segments, length, offset, key );
}


/* do an ioctl call through the server */
static NTSTATUS server_ioctl_file( HANDLE handle, HANDLE event,
                                   PIO_APC_ROUTINE apc, PVOID apc_context,
                                   IO_STATUS_BLOCK *io, ULONG code,
                                   const void *in_buffer, ULONG in_size,
                                   PVOID out_buffer, ULONG out_size )
{
    struct async_irp *async;
    NTSTATUS status;
    HANDLE wait_handle;
    ULONG options;

    if (!(async = (struct async_irp *)alloc_fileio( sizeof(*async), irp_completion, handle )))
        return STATUS_NO_MEMORY;
    async->buffer  = out_buffer;
    async->size    = out_size;

    SERVER_START_REQ( ioctl )
    {
        req->code  = code;
        req->async = server_async( handle, &async->io, event, apc, apc_context, io );
        wine_server_add_data( req, in_buffer, in_size );
        if ((code & 3) != METHOD_BUFFERED)
            wine_server_add_data( req, out_buffer, out_size );
        wine_server_set_reply( req, out_buffer, out_size );
        status = unix_funcs->virtual_locked_server_call( req );
        wait_handle = wine_server_ptr_handle( reply->wait );
        options     = reply->options;
        if (wait_handle && status != STATUS_PENDING)
        {
            io->u.Status    = status;
            io->Information = wine_server_reply_size( reply );
        }
    }
    SERVER_END_REQ;

    if (status == STATUS_NOT_SUPPORTED)
        FIXME("Unsupported ioctl %x (device=%x access=%x func=%x method=%x)\n",
              code, code >> 16, (code >> 14) & 3, (code >> 2) & 0xfff, code & 3);

    if (status != STATUS_PENDING) RtlFreeHeap( GetProcessHeap(), 0, async );

    if (wait_handle) status = wait_async( wait_handle, (options & FILE_SYNCHRONOUS_IO_ALERT), io );
    return status;
}

/* Tell Valgrind to ignore any holes in structs we will be passing to the
 * server */
static void ignore_server_ioctl_struct_holes (ULONG code, const void *in_buffer,
                                              ULONG in_size)
{
#ifdef VALGRIND_MAKE_MEM_DEFINED
# define IGNORE_STRUCT_HOLE(buf, size, t, f1, f2) \
    do { \
        if (FIELD_OFFSET(t, f1) + sizeof(((t *)0)->f1) < FIELD_OFFSET(t, f2)) \
            if ((size) >= FIELD_OFFSET(t, f2)) \
                VALGRIND_MAKE_MEM_DEFINED( \
                    (const char *)(buf) + FIELD_OFFSET(t, f1) + sizeof(((t *)0)->f1), \
                    FIELD_OFFSET(t, f2) - FIELD_OFFSET(t, f1) + sizeof(((t *)0)->f1)); \
    } while (0)

    switch (code)
    {
    case FSCTL_PIPE_WAIT:
        IGNORE_STRUCT_HOLE(in_buffer, in_size, FILE_PIPE_WAIT_FOR_BUFFER, TimeoutSpecified, Name);
        break;
    }
#endif
}


/**************************************************************************
 *		NtDeviceIoControlFile			[NTDLL.@]
 *		ZwDeviceIoControlFile			[NTDLL.@]
 *
 * Perform an I/O control operation on an open file handle.
 *
 * PARAMS
 *  handle         [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  event          [I] Event to signal upon completion (or NULL)
 *  apc            [I] Callback to call upon completion (or NULL)
 *  apc_context    [I] Context for ApcRoutine (or NULL)
 *  io             [O] Receives information about the operation on return
 *  code           [I] Control code for the operation to perform
 *  in_buffer      [I] Source for any input data required (or NULL)
 *  in_size        [I] Size of InputBuffer
 *  out_buffer     [O] Source for any output data returned (or NULL)
 *  out_size       [I] Size of OutputBuffer
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtDeviceIoControlFile(HANDLE handle, HANDLE event,
                                      PIO_APC_ROUTINE apc, PVOID apc_context,
                                      PIO_STATUS_BLOCK io, ULONG code,
                                      PVOID in_buffer, ULONG in_size,
                                      PVOID out_buffer, ULONG out_size)
{
    ULONG device = (code >> 16);
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    TRACE("(%p,%p,%p,%p,%p,0x%08x,%p,0x%08x,%p,0x%08x)\n",
          handle, event, apc, apc_context, io, code,
          in_buffer, in_size, out_buffer, out_size);

    switch(device)
    {
    case FILE_DEVICE_DISK:
    case FILE_DEVICE_CD_ROM:
    case FILE_DEVICE_DVD:
    case FILE_DEVICE_CONTROLLER:
    case FILE_DEVICE_MASS_STORAGE:
        status = CDROM_DeviceIoControl(handle, event, apc, apc_context, io, code,
                                       in_buffer, in_size, out_buffer, out_size);
        break;
    case FILE_DEVICE_SERIAL_PORT:
        status = COMM_DeviceIoControl(handle, event, apc, apc_context, io, code,
                                      in_buffer, in_size, out_buffer, out_size);
        break;
    case FILE_DEVICE_TAPE:
        status = TAPE_DeviceIoControl(handle, event, apc, apc_context, io, code,
                                      in_buffer, in_size, out_buffer, out_size);
        break;
    }

    if (status == STATUS_NOT_SUPPORTED || status == STATUS_BAD_DEVICE_TYPE)
        return server_ioctl_file( handle, event, apc, apc_context, io, code,
                                  in_buffer, in_size, out_buffer, out_size );

    if (status != STATUS_PENDING) io->u.Status = status;
    return status;
}


/**************************************************************************
 *              NtFsControlFile                 [NTDLL.@]
 *              ZwFsControlFile                 [NTDLL.@]
 *
 * Perform a file system control operation on an open file handle.
 *
 * PARAMS
 *  handle         [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  event          [I] Event to signal upon completion (or NULL)
 *  apc            [I] Callback to call upon completion (or NULL)
 *  apc_context    [I] Context for ApcRoutine (or NULL)
 *  io             [O] Receives information about the operation on return
 *  code           [I] Control code for the operation to perform
 *  in_buffer      [I] Source for any input data required (or NULL)
 *  in_size        [I] Size of InputBuffer
 *  out_buffer     [O] Source for any output data returned (or NULL)
 *  out_size       [I] Size of OutputBuffer
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtFsControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc,
                                PVOID apc_context, PIO_STATUS_BLOCK io, ULONG code,
                                PVOID in_buffer, ULONG in_size, PVOID out_buffer, ULONG out_size)
{
    NTSTATUS status;

    TRACE("(%p,%p,%p,%p,%p,0x%08x,%p,0x%08x,%p,0x%08x)\n",
          handle, event, apc, apc_context, io, code,
          in_buffer, in_size, out_buffer, out_size);

    if (!io) return STATUS_INVALID_PARAMETER;

    ignore_server_ioctl_struct_holes( code, in_buffer, in_size );

    switch(code)
    {
    case FSCTL_DISMOUNT_VOLUME:
        status = server_ioctl_file( handle, event, apc, apc_context, io, code,
                                    in_buffer, in_size, out_buffer, out_size );
        if (!status) status = unix_funcs->unmount_device( handle );
        return status;

    case FSCTL_PIPE_IMPERSONATE:
        FIXME("FSCTL_PIPE_IMPERSONATE: impersonating self\n");
        status = RtlImpersonateSelf( SecurityImpersonation );
        break;

    case FSCTL_IS_VOLUME_MOUNTED:
    case FSCTL_LOCK_VOLUME:
    case FSCTL_UNLOCK_VOLUME:
        FIXME("stub! return success - Unsupported fsctl %x (device=%x access=%x func=%x method=%x)\n",
              code, code >> 16, (code >> 14) & 3, (code >> 2) & 0xfff, code & 3);
        status = STATUS_SUCCESS;
        break;

    case FSCTL_GET_RETRIEVAL_POINTERS:
    {
        RETRIEVAL_POINTERS_BUFFER *buffer = (RETRIEVAL_POINTERS_BUFFER *)out_buffer;

        FIXME("stub: FSCTL_GET_RETRIEVAL_POINTERS\n");

        if (out_size >= sizeof(RETRIEVAL_POINTERS_BUFFER))
        {
            buffer->ExtentCount                 = 1;
            buffer->StartingVcn.QuadPart        = 1;
            buffer->Extents[0].NextVcn.QuadPart = 0;
            buffer->Extents[0].Lcn.QuadPart     = 0;
            io->Information = sizeof(RETRIEVAL_POINTERS_BUFFER);
            status = STATUS_SUCCESS;
        }
        else
        {
            io->Information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }
    case FSCTL_SET_SPARSE:
        TRACE("FSCTL_SET_SPARSE: Ignoring request\n");
        io->Information = 0;
        status = STATUS_SUCCESS;
        break;
    default:
        return server_ioctl_file( handle, event, apc, apc_context, io, code,
                                  in_buffer, in_size, out_buffer, out_size );
    }

    if (status != STATUS_PENDING) io->u.Status = status;
    return status;
}


struct read_changes_fileio
{
    struct async_fileio io;
    void               *buffer;
    ULONG               buffer_size;
    ULONG               data_size;
    char                data[1];
};

static NTSTATUS read_changes_apc( void *user, IO_STATUS_BLOCK *iosb, NTSTATUS status )
{
    struct read_changes_fileio *fileio = user;
    int size = 0;

    if (status == STATUS_ALERTED)
    {
        SERVER_START_REQ( read_change )
        {
            req->handle = wine_server_obj_handle( fileio->io.handle );
            wine_server_set_reply( req, fileio->data, fileio->data_size );
            status = wine_server_call( req );
            size = wine_server_reply_size( reply );
        }
        SERVER_END_REQ;

        if (status == STATUS_SUCCESS && fileio->buffer)
        {
            FILE_NOTIFY_INFORMATION *pfni = fileio->buffer;
            int i, left = fileio->buffer_size;
            DWORD *last_entry_offset = NULL;
            struct filesystem_event *event = (struct filesystem_event*)fileio->data;

            while (size && left >= sizeof(*pfni))
            {
                DWORD len = (left - offsetof(FILE_NOTIFY_INFORMATION, FileName)) / sizeof(WCHAR);

                /* convert to an NT style path */
                for (i = 0; i < event->len; i++)
                    if (event->name[i] == '/') event->name[i] = '\\';

                pfni->Action = event->action;
                pfni->FileNameLength = ntdll_umbstowcs( event->name, event->len, pfni->FileName, len );
                last_entry_offset = &pfni->NextEntryOffset;

                if (pfni->FileNameLength == len) break;

                i = offsetof(FILE_NOTIFY_INFORMATION, FileName[pfni->FileNameLength]);
                pfni->FileNameLength *= sizeof(WCHAR);
                pfni->NextEntryOffset = i;
                pfni = (FILE_NOTIFY_INFORMATION*)((char*)pfni + i);
                left -= i;

                i = (offsetof(struct filesystem_event, name[event->len])
                     + sizeof(int)-1) / sizeof(int) * sizeof(int);
                event = (struct filesystem_event*)((char*)event + i);
                size -= i;
            }

            if (size)
            {
                status = STATUS_NOTIFY_ENUM_DIR;
                size = 0;
            }
            else
            {
                if (last_entry_offset) *last_entry_offset = 0;
                size = fileio->buffer_size - left;
            }
        }
        else
        {
            status = STATUS_NOTIFY_ENUM_DIR;
            size = 0;
        }
    }

    if (status != STATUS_PENDING)
    {
        iosb->u.Status = status;
        iosb->Information = size;
        release_fileio( &fileio->io );
    }
    return status;
}

#define FILE_NOTIFY_ALL        (  \
 FILE_NOTIFY_CHANGE_FILE_NAME   | \
 FILE_NOTIFY_CHANGE_DIR_NAME    | \
 FILE_NOTIFY_CHANGE_ATTRIBUTES  | \
 FILE_NOTIFY_CHANGE_SIZE        | \
 FILE_NOTIFY_CHANGE_LAST_WRITE  | \
 FILE_NOTIFY_CHANGE_LAST_ACCESS | \
 FILE_NOTIFY_CHANGE_CREATION    | \
 FILE_NOTIFY_CHANGE_SECURITY   )

/******************************************************************************
 *  NtNotifyChangeDirectoryFile [NTDLL.@]
 */
NTSTATUS WINAPI NtNotifyChangeDirectoryFile( HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc,
                                             void *apc_context, PIO_STATUS_BLOCK iosb, void *buffer,
                                             ULONG buffer_size, ULONG filter, BOOLEAN subtree )
{
    struct read_changes_fileio *fileio;
    NTSTATUS status;
    ULONG size = max( 4096, buffer_size );

    TRACE( "%p %p %p %p %p %p %u %u %d\n",
           handle, event, apc, apc_context, iosb, buffer, buffer_size, filter, subtree );

    if (!iosb) return STATUS_ACCESS_VIOLATION;
    if (filter == 0 || (filter & ~FILE_NOTIFY_ALL)) return STATUS_INVALID_PARAMETER;

    fileio = (struct read_changes_fileio *)alloc_fileio( offsetof(struct read_changes_fileio, data[size]),
                                                         read_changes_apc, handle );
    if (!fileio) return STATUS_NO_MEMORY;

    fileio->buffer      = buffer;
    fileio->buffer_size = buffer_size;
    fileio->data_size   = size;

    SERVER_START_REQ( read_directory_changes )
    {
        req->filter    = filter;
        req->want_data = (buffer != NULL);
        req->subtree   = subtree;
        req->async     = server_async( handle, &fileio->io, event, apc, apc_context, iosb );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    if (status != STATUS_PENDING) RtlFreeHeap( GetProcessHeap(), 0, fileio );
    return status;
}

/******************************************************************************
 *  NtSetVolumeInformationFile		[NTDLL.@]
 *  ZwSetVolumeInformationFile		[NTDLL.@]
 *
 * Set volume information for an open file handle.
 *
 * PARAMS
 *  FileHandle         [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  IoStatusBlock      [O] Receives information about the operation on return
 *  FsInformation      [I] Source for volume information
 *  Length             [I] Size of FsInformation
 *  FsInformationClass [I] Type of volume information to set
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtSetVolumeInformationFile(
	IN HANDLE FileHandle,
	PIO_STATUS_BLOCK IoStatusBlock,
	PVOID FsInformation,
        ULONG Length,
	FS_INFORMATION_CLASS FsInformationClass)
{
	FIXME("(%p,%p,%p,0x%08x,0x%08x) stub\n",
	FileHandle,IoStatusBlock,FsInformation,Length,FsInformationClass);
	return 0;
}

NTSTATUS server_get_unix_name( HANDLE handle, ANSI_STRING *unix_name )
{
    data_size_t size = 1024;
    NTSTATUS ret;
    char *name;

    for (;;)
    {
        name = RtlAllocateHeap( GetProcessHeap(), 0, size + 1 );
        if (!name) return STATUS_NO_MEMORY;
        unix_name->MaximumLength = size + 1;

        SERVER_START_REQ( get_handle_unix_name )
        {
            req->handle = wine_server_obj_handle( handle );
            wine_server_set_reply( req, name, size );
            ret = wine_server_call( req );
            size = reply->name_len;
        }
        SERVER_END_REQ;

        if (!ret)
        {
            name[size] = 0;
            unix_name->Buffer = name;
            unix_name->Length = size;
            break;
        }
        RtlFreeHeap( GetProcessHeap(), 0, name );
        if (ret != STATUS_BUFFER_OVERFLOW) break;
    }
    return ret;
}

/* Find a DOS device which can act as the root of "path".
 * Similar to find_drive_root(), but returns -1 instead of crossing volumes. */
static int find_dos_device( const char *path )
{
    int len = strlen(path);
    int drive;
    char *buffer;
    struct stat st;
    struct drive_info info[MAX_DOS_DRIVES];
    dev_t dev_id;

    if (!DIR_get_drives_info( info )) return -1;

    if (stat( path, &st ) < 0) return -1;
    dev_id = st.st_dev;

    /* strip off trailing slashes */
    while (len > 1 && path[len - 1] == '/') len--;

    /* make a copy of the path */
    if (!(buffer = RtlAllocateHeap( GetProcessHeap(), 0, len + 1 ))) return -1;
    memcpy( buffer, path, len );
    buffer[len] = 0;

    for (;;)
    {
        if (!stat( buffer, &st ) && S_ISDIR( st.st_mode ))
        {
            if (st.st_dev != dev_id) break;

            for (drive = 0; drive < MAX_DOS_DRIVES; drive++)
            {
                if ((info[drive].dev == st.st_dev) && (info[drive].ino == st.st_ino))
                {
                    if (len == 1) len = 0;  /* preserve root slash in returned path */
                    TRACE( "%s -> drive %c:, root=%s, name=%s\n",
                           debugstr_a(path), 'A' + drive, debugstr_a(buffer), debugstr_a(path + len));
                    RtlFreeHeap( GetProcessHeap(), 0, buffer );
                    return drive;
                }
            }
        }
        if (len <= 1) break;  /* reached root */
        while (path[len - 1] != '/') len--;
        while (path[len - 1] == '/') len--;
        buffer[len] = 0;
    }
    RtlFreeHeap( GetProcessHeap(), 0, buffer );
    return -1;
}

static struct mountmgr_unix_drive *get_mountmgr_fs_info( HANDLE handle, int fd )
{
    struct mountmgr_unix_drive *drive;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING string;
    ANSI_STRING unix_name;
    IO_STATUS_BLOCK io;
    HANDLE mountmgr;
    NTSTATUS status;
    int letter;

    if (server_get_unix_name( handle, &unix_name ))
        return NULL;

    letter = find_dos_device( unix_name.Buffer );
    RtlFreeAnsiString( &unix_name );

    if (!(drive = RtlAllocateHeap( GetProcessHeap(), 0, 1024 )))
        return NULL;

    if (letter == -1)
    {
        struct stat st;

        if (fstat( fd, &st ) == -1)
        {
            RtlFreeHeap( GetProcessHeap(), 0, drive );
            return NULL;
        }

        drive->unix_dev = st.st_dev;
        drive->letter = 0;
    }
    else
        drive->letter = 'a' + letter;

    RtlInitUnicodeString( &string, MOUNTMGR_DEVICE_NAME );
    InitializeObjectAttributes( &attr, &string, 0, NULL, NULL );
    if (NtOpenFile( &mountmgr, GENERIC_READ | SYNCHRONIZE, &attr, &io,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT ))
    {
        RtlFreeHeap( GetProcessHeap(), 0, drive );
        return NULL;
    }

    status = NtDeviceIoControlFile( mountmgr, NULL, NULL, NULL, &io, IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE,
                                    drive, sizeof(*drive), drive, 1024 );
    if (status == STATUS_BUFFER_OVERFLOW)
    {
        if (!(drive = RtlReAllocateHeap( GetProcessHeap(), 0, drive, drive->size )))
        {
            RtlFreeHeap( GetProcessHeap(), 0, drive );
            NtClose( mountmgr );
            return NULL;
        }
        status = NtDeviceIoControlFile( mountmgr, NULL, NULL, NULL, &io, IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE,
                                        drive, sizeof(*drive), drive, drive->size );
    }
    NtClose( mountmgr );

    if (status)
    {
        WARN("failed to retrieve filesystem type from mountmgr, status %#x\n", status);
        RtlFreeHeap( GetProcessHeap(), 0, drive );
        return NULL;
    }

    return drive;
}

/******************************************************************************
 *  NtQueryInformationFile		[NTDLL.@]
 *  ZwQueryInformationFile		[NTDLL.@]
 *
 * Get information about an open file handle.
 *
 * PARAMS
 *  hFile    [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  io       [O] Receives information about the operation on return
 *  ptr      [O] Destination for file information
 *  len      [I] Size of FileInformation
 *  class    [I] Type of file information to get
 *
 * RETURNS
 *  Success: 0. IoStatusBlock and FileInformation are updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtQueryInformationFile( HANDLE hFile, PIO_STATUS_BLOCK io,
                                        PVOID ptr, LONG len, FILE_INFORMATION_CLASS class )
{
    return unix_funcs->NtQueryInformationFile( hFile, io, ptr, len, class );
}

/******************************************************************************
 *  NtSetInformationFile		[NTDLL.@]
 *  ZwSetInformationFile		[NTDLL.@]
 *
 * Set information about an open file handle.
 *
 * PARAMS
 *  handle  [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  io      [O] Receives information about the operation on return
 *  ptr     [I] Source for file information
 *  len     [I] Size of FileInformation
 *  class   [I] Type of file information to set
 *
 * RETURNS
 *  Success: 0. io is updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtSetInformationFile(HANDLE handle, PIO_STATUS_BLOCK io,
                                     PVOID ptr, ULONG len, FILE_INFORMATION_CLASS class)
{
    return unix_funcs->NtSetInformationFile( handle, io, ptr, len, class );
}


/******************************************************************************
 *              NtQueryFullAttributesFile   (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryFullAttributesFile( const OBJECT_ATTRIBUTES *attr,
                                           FILE_NETWORK_OPEN_INFORMATION *info )
{
    return unix_funcs->NtQueryFullAttributesFile( attr, info );
}


/******************************************************************************
 *              NtQueryAttributesFile   (NTDLL.@)
 *              ZwQueryAttributesFile   (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryAttributesFile( const OBJECT_ATTRIBUTES *attr, FILE_BASIC_INFORMATION *info )
{
    return unix_funcs->NtQueryAttributesFile( attr, info );
}


#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
/* helper for FILE_GetDeviceInfo to hide some platform differences in fstatfs */
static inline void get_device_info_fstatfs( FILE_FS_DEVICE_INFORMATION *info, const char *fstypename,
                                            unsigned int flags )
{
    if (!strcmp("cd9660", fstypename) || !strcmp("udf", fstypename))
    {
        info->DeviceType = FILE_DEVICE_CD_ROM_FILE_SYSTEM;
        /* Don't assume read-only, let the mount options set it below */
        info->Characteristics |= FILE_REMOVABLE_MEDIA;
    }
    else if (!strcmp("nfs", fstypename) || !strcmp("nwfs", fstypename) ||
             !strcmp("smbfs", fstypename) || !strcmp("afpfs", fstypename))
    {
        info->DeviceType = FILE_DEVICE_NETWORK_FILE_SYSTEM;
        info->Characteristics |= FILE_REMOTE_DEVICE;
    }
    else if (!strcmp("procfs", fstypename))
        info->DeviceType = FILE_DEVICE_VIRTUAL_DISK;
    else
        info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;

    if (flags & MNT_RDONLY)
        info->Characteristics |= FILE_READ_ONLY_DEVICE;

    if (!(flags & MNT_LOCAL))
    {
        info->DeviceType = FILE_DEVICE_NETWORK_FILE_SYSTEM;
        info->Characteristics |= FILE_REMOTE_DEVICE;
    }
}
#endif

static inline BOOL is_device_placeholder( int fd )
{
    static const char wine_placeholder[] = "Wine device placeholder";
    char buffer[sizeof(wine_placeholder)-1];

    if (pread( fd, buffer, sizeof(wine_placeholder) - 1, 0 ) != sizeof(wine_placeholder) - 1)
        return FALSE;
    return !memcmp( buffer, wine_placeholder, sizeof(wine_placeholder) - 1 );
}

/******************************************************************************
 *              get_device_info
 *
 * Implementation of the FileFsDeviceInformation query for NtQueryVolumeInformationFile.
 */
static NTSTATUS get_device_info( int fd, FILE_FS_DEVICE_INFORMATION *info )
{
    struct stat st;

    info->Characteristics = 0;
    if (fstat( fd, &st ) < 0) return FILE_GetNtStatus();
    if (S_ISCHR( st.st_mode ))
    {
        info->DeviceType = FILE_DEVICE_UNKNOWN;
#ifdef linux
        switch(major(st.st_rdev))
        {
        case MEM_MAJOR:
            info->DeviceType = FILE_DEVICE_NULL;
            break;
        case TTY_MAJOR:
            info->DeviceType = FILE_DEVICE_SERIAL_PORT;
            break;
        case LP_MAJOR:
            info->DeviceType = FILE_DEVICE_PARALLEL_PORT;
            break;
        case SCSI_TAPE_MAJOR:
            info->DeviceType = FILE_DEVICE_TAPE;
            break;
        }
#endif
    }
    else if (S_ISBLK( st.st_mode ))
    {
        info->DeviceType = FILE_DEVICE_DISK;
    }
    else if (S_ISFIFO( st.st_mode ) || S_ISSOCK( st.st_mode ))
    {
        info->DeviceType = FILE_DEVICE_NAMED_PIPE;
    }
    else if (is_device_placeholder( fd ))
    {
        info->DeviceType = FILE_DEVICE_DISK;
    }
    else  /* regular file or directory */
    {
#if defined(linux) && defined(HAVE_FSTATFS)
        struct statfs stfs;

        /* check for floppy disk */
        if (major(st.st_dev) == FLOPPY_MAJOR)
            info->Characteristics |= FILE_REMOVABLE_MEDIA;

        if (fstatfs( fd, &stfs ) < 0) stfs.f_type = 0;
        switch (stfs.f_type)
        {
        case 0x9660:      /* iso9660 */
        case 0x9fa1:      /* supermount */
        case 0x15013346:  /* udf */
            info->DeviceType = FILE_DEVICE_CD_ROM_FILE_SYSTEM;
            info->Characteristics |= FILE_REMOVABLE_MEDIA|FILE_READ_ONLY_DEVICE;
            break;
        case 0x6969:  /* nfs */
        case 0xff534d42: /* cifs */
        case 0xfe534d42: /* smb2 */
        case 0x517b:  /* smbfs */
        case 0x564c:  /* ncpfs */
            info->DeviceType = FILE_DEVICE_NETWORK_FILE_SYSTEM;
            info->Characteristics |= FILE_REMOTE_DEVICE;
            break;
        case 0x01021994:  /* tmpfs */
        case 0x28cd3d45:  /* cramfs */
        case 0x1373:      /* devfs */
        case 0x9fa0:      /* procfs */
            info->DeviceType = FILE_DEVICE_VIRTUAL_DISK;
            break;
        default:
            info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
            break;
        }
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
        struct statfs stfs;

        if (fstatfs( fd, &stfs ) < 0)
            info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
        else
            get_device_info_fstatfs( info, stfs.f_fstypename, stfs.f_flags );
#elif defined(__NetBSD__)
        struct statvfs stfs;

        if (fstatvfs( fd, &stfs) < 0)
            info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
        else
            get_device_info_fstatfs( info, stfs.f_fstypename, stfs.f_flag );
#elif defined(sun)
        /* Use dkio to work out device types */
        {
# include <sys/dkio.h>
# include <sys/vtoc.h>
            struct dk_cinfo dkinf;
            int retval = ioctl(fd, DKIOCINFO, &dkinf);
            if(retval==-1){
                WARN("Unable to get disk device type information - assuming a disk like device\n");
                info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
            }
            switch (dkinf.dki_ctype)
            {
            case DKC_CDROM:
                info->DeviceType = FILE_DEVICE_CD_ROM_FILE_SYSTEM;
                info->Characteristics |= FILE_REMOVABLE_MEDIA|FILE_READ_ONLY_DEVICE;
                break;
            case DKC_NCRFLOPPY:
            case DKC_SMSFLOPPY:
            case DKC_INTEL82072:
            case DKC_INTEL82077:
                info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
                info->Characteristics |= FILE_REMOVABLE_MEDIA;
                break;
            case DKC_MD:
                info->DeviceType = FILE_DEVICE_VIRTUAL_DISK;
                break;
            default:
                info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
            }
        }
#else
        static int warned;
        if (!warned++) FIXME( "device info not properly supported on this platform\n" );
        info->DeviceType = FILE_DEVICE_DISK_FILE_SYSTEM;
#endif
        info->Characteristics |= FILE_DEVICE_IS_MOUNTED;
    }
    return STATUS_SUCCESS;
}

/******************************************************************************
 *  NtQueryVolumeInformationFile		[NTDLL.@]
 *  ZwQueryVolumeInformationFile		[NTDLL.@]
 *
 * Get volume information for an open file handle.
 *
 * PARAMS
 *  handle      [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  io          [O] Receives information about the operation on return
 *  buffer      [O] Destination for volume information
 *  length      [I] Size of FsInformation
 *  info_class  [I] Type of volume information to set
 *
 * RETURNS
 *  Success: 0. io and buffer are updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtQueryVolumeInformationFile( HANDLE handle, PIO_STATUS_BLOCK io,
                                              PVOID buffer, ULONG length,
                                              FS_INFORMATION_CLASS info_class )
{
    int fd, needs_close;
    struct stat st;

    io->u.Status = unix_funcs->server_get_unix_fd( handle, 0, &fd, &needs_close, NULL, NULL );
    if (io->u.Status == STATUS_BAD_DEVICE_TYPE)
    {
        SERVER_START_REQ( get_volume_info )
        {
            req->handle = wine_server_obj_handle( handle );
            req->info_class = info_class;
            wine_server_set_reply( req, buffer, length );
            io->u.Status = wine_server_call( req );
            if (!io->u.Status) io->Information = wine_server_reply_size( reply );
        }
        SERVER_END_REQ;
        return io->u.Status;
    }
    else if (io->u.Status) return io->u.Status;

    io->u.Status = STATUS_NOT_IMPLEMENTED;
    io->Information = 0;

    switch( info_class )
    {
    case FileFsLabelInformation:
        FIXME( "%p: label info not supported\n", handle );
        break;
    case FileFsSizeInformation:
        if (length < sizeof(FILE_FS_SIZE_INFORMATION))
            io->u.Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            FILE_FS_SIZE_INFORMATION *info = buffer;

            if (fstat( fd, &st ) < 0)
            {
                io->u.Status = FILE_GetNtStatus();
                break;
            }
            if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
            {
                io->u.Status = STATUS_INVALID_DEVICE_REQUEST;
            }
            else
            {
                ULONGLONG bsize;
                /* Linux's fstatvfs is buggy */
#if !defined(linux) || !defined(HAVE_FSTATFS)
                struct statvfs stfs;

                if (fstatvfs( fd, &stfs ) < 0)
                {
                    io->u.Status = FILE_GetNtStatus();
                    break;
                }
                bsize = stfs.f_frsize;
#else
                struct statfs stfs;
                if (fstatfs( fd, &stfs ) < 0)
                {
                    io->u.Status = FILE_GetNtStatus();
                    break;
                }
                bsize = stfs.f_bsize;
#endif
                if (bsize == 2048)  /* assume CD-ROM */
                {
                    info->BytesPerSector = 2048;
                    info->SectorsPerAllocationUnit = 1;
                }
                else
                {
                    info->BytesPerSector = 512;
                    info->SectorsPerAllocationUnit = 8;
                }
                info->TotalAllocationUnits.QuadPart = bsize * stfs.f_blocks / (info->BytesPerSector * info->SectorsPerAllocationUnit);
                info->AvailableAllocationUnits.QuadPart = bsize * stfs.f_bavail / (info->BytesPerSector * info->SectorsPerAllocationUnit);
                io->Information = sizeof(*info);
                io->u.Status = STATUS_SUCCESS;
            }
        }
        break;
    case FileFsDeviceInformation:
        if (length < sizeof(FILE_FS_DEVICE_INFORMATION))
            io->u.Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            FILE_FS_DEVICE_INFORMATION *info = buffer;

            if ((io->u.Status = get_device_info( fd, info )) == STATUS_SUCCESS)
                io->Information = sizeof(*info);
        }
        break;
    case FileFsAttributeInformation:
    {
        static const WCHAR fatW[] = {'F','A','T'};
        static const WCHAR fat32W[] = {'F','A','T','3','2'};
        static const WCHAR ntfsW[] = {'N','T','F','S'};
        static const WCHAR cdfsW[] = {'C','D','F','S'};
        static const WCHAR udfW[] = {'U','D','F'};

        FILE_FS_ATTRIBUTE_INFORMATION *info = buffer;
        struct mountmgr_unix_drive *drive;
        enum mountmgr_fs_type fs_type = MOUNTMGR_FS_TYPE_NTFS;

        if (length < sizeof(FILE_FS_ATTRIBUTE_INFORMATION))
        {
            io->u.Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }

        if ((drive = get_mountmgr_fs_info( handle, fd )))
        {
            fs_type = drive->fs_type;
            RtlFreeHeap( GetProcessHeap(), 0, drive );
        }
        else
        {
            struct statfs stfs;

            if (!fstatfs( fd, &stfs ))
            {
#if defined(linux) && defined(HAVE_FSTATFS)
                switch (stfs.f_type)
                {
                case 0x9660:
                    fs_type = MOUNTMGR_FS_TYPE_ISO9660;
                    break;
                case 0x15013346:
                    fs_type = MOUNTMGR_FS_TYPE_UDF;
                    break;
                case 0x4d44:
                    fs_type = MOUNTMGR_FS_TYPE_FAT32;
                    break;
                }
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
                if (!strcmp( stfs.f_fstypename, "cd9660" ))
                    fs_type = MOUNTMGR_FS_TYPE_ISO9660;
                else if (!strcmp( stfs.f_fstypename, "udf" ))
                    fs_type = MOUNTMGR_FS_TYPE_UDF;
                else if (!strcmp( stfs.f_fstypename, "msdos" ))
                    fs_type = MOUNTMGR_FS_TYPE_FAT32;
#endif
            }
        }

        switch (fs_type)
        {
        case MOUNTMGR_FS_TYPE_ISO9660:
            info->FileSystemAttributes = FILE_READ_ONLY_VOLUME;
            info->MaximumComponentNameLength = 221;
            info->FileSystemNameLength = min( sizeof(cdfsW), length - offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) );
            memcpy(info->FileSystemName, cdfsW, info->FileSystemNameLength);
            break;
        case MOUNTMGR_FS_TYPE_UDF:
            info->FileSystemAttributes = FILE_READ_ONLY_VOLUME | FILE_UNICODE_ON_DISK | FILE_CASE_SENSITIVE_SEARCH;
            info->MaximumComponentNameLength = 255;
            info->FileSystemNameLength = min( sizeof(udfW), length - offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) );
            memcpy(info->FileSystemName, udfW, info->FileSystemNameLength);
            break;
        case MOUNTMGR_FS_TYPE_FAT:
            info->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES; /* FIXME */
            info->MaximumComponentNameLength = 255;
            info->FileSystemNameLength = min( sizeof(fatW), length - offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) );
            memcpy(info->FileSystemName, fatW, info->FileSystemNameLength);
            break;
        case MOUNTMGR_FS_TYPE_FAT32:
            info->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES; /* FIXME */
            info->MaximumComponentNameLength = 255;
            info->FileSystemNameLength = min( sizeof(fat32W), length - offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) );
            memcpy(info->FileSystemName, fat32W, info->FileSystemNameLength);
            break;
        default:
            info->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES | FILE_PERSISTENT_ACLS;
            info->MaximumComponentNameLength = 255;
            info->FileSystemNameLength = min( sizeof(ntfsW), length - offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) );
            memcpy(info->FileSystemName, ntfsW, info->FileSystemNameLength);
            break;
        }

        io->Information = offsetof( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName ) + info->FileSystemNameLength;
        io->u.Status = STATUS_SUCCESS;
        break;
    }
    case FileFsVolumeInformation:
    {
        FILE_FS_VOLUME_INFORMATION *info = buffer;
        struct mountmgr_unix_drive *drive;
        const WCHAR *label;

        if (length < sizeof(FILE_FS_VOLUME_INFORMATION))
        {
            io->u.Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }

        if (!(drive = get_mountmgr_fs_info( handle, fd )))
        {
            io->u.Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        label = (WCHAR *)((char *)drive + drive->label_offset);
        info->VolumeCreationTime.QuadPart = 0; /* FIXME */
        info->VolumeSerialNumber = drive->serial;
        info->VolumeLabelLength = min( wcslen( label ) * sizeof(WCHAR),
                                       length - offsetof( FILE_FS_VOLUME_INFORMATION, VolumeLabel ) );
        info->SupportsObjects = (drive->fs_type == MOUNTMGR_FS_TYPE_NTFS);
        memcpy( info->VolumeLabel, label, info->VolumeLabelLength );
        RtlFreeHeap( GetProcessHeap(), 0, drive );

        io->Information = offsetof( FILE_FS_VOLUME_INFORMATION, VolumeLabel ) + info->VolumeLabelLength;
        io->u.Status = STATUS_SUCCESS;
        break;
    }
    case FileFsControlInformation:
        FIXME( "%p: control info not supported\n", handle );
        break;
    case FileFsFullSizeInformation:
        FIXME( "%p: full size info not supported\n", handle );
        break;
    case FileFsObjectIdInformation:
        FIXME( "%p: object id info not supported\n", handle );
        break;
    case FileFsMaximumInformation:
        FIXME( "%p: maximum info not supported\n", handle );
        break;
    default:
        io->u.Status = STATUS_INVALID_PARAMETER;
        break;
    }
    if (needs_close) close( fd );
    return io->u.Status;
}


/******************************************************************
 *		NtQueryEaFile  (NTDLL.@)
 *
 * Read extended attributes from NTFS files.
 *
 * PARAMS
 *  hFile         [I] File handle, must be opened with FILE_READ_EA access
 *  iosb          [O] Receives information about the operation on return
 *  buffer        [O] Output buffer
 *  length        [I] Length of output buffer
 *  single_entry  [I] Only read and return one entry
 *  ea_list       [I] Optional list with names of EAs to return
 *  ea_list_len   [I] Length of ea_list in bytes
 *  ea_index      [I] Optional pointer to 1-based index of attribute to return
 *  restart       [I] restart EA scan
 *
 * RETURNS
 *  Success: 0. Attributes read into buffer
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtQueryEaFile( HANDLE hFile, PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length,
                               BOOLEAN single_entry, PVOID ea_list, ULONG ea_list_len,
                               PULONG ea_index, BOOLEAN restart )
{
    FIXME("(%p,%p,%p,%d,%d,%p,%d,%p,%d) stub\n",
            hFile, iosb, buffer, length, single_entry, ea_list,
            ea_list_len, ea_index, restart);
    return STATUS_ACCESS_DENIED;
}


/******************************************************************
 *		NtSetEaFile  (NTDLL.@)
 *
 * Update extended attributes for NTFS files.
 *
 * PARAMS
 *  hFile         [I] File handle, must be opened with FILE_READ_EA access
 *  iosb          [O] Receives information about the operation on return
 *  buffer        [I] Buffer with EA information
 *  length        [I] Length of buffer
 *
 * RETURNS
 *  Success: 0. Attributes are updated
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtSetEaFile( HANDLE hFile, PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length )
{
    FIXME("(%p,%p,%p,%d) stub\n", hFile, iosb, buffer, length);
    return STATUS_ACCESS_DENIED;
}


/******************************************************************
 *		NtFlushBuffersFile  (NTDLL.@)
 *
 * Flush any buffered data on an open file handle.
 *
 * PARAMS
 *  FileHandle         [I] Handle returned from ZwOpenFile() or ZwCreateFile()
 *  IoStatusBlock      [O] Receives information about the operation on return
 *
 * RETURNS
 *  Success: 0. IoStatusBlock is updated.
 *  Failure: An NTSTATUS error code describing the error.
 */
NTSTATUS WINAPI NtFlushBuffersFile( HANDLE hFile, IO_STATUS_BLOCK *io )
{
    NTSTATUS ret;
    HANDLE wait_handle;
    enum server_fd_type type;
    int fd, needs_close;

    if (!io || !unix_funcs->virtual_check_buffer_for_write( io, sizeof(*io) )) return STATUS_ACCESS_VIOLATION;

    ret = unix_funcs->server_get_unix_fd( hFile, FILE_WRITE_DATA, &fd, &needs_close, &type, NULL );
    if (ret == STATUS_ACCESS_DENIED)
        ret = unix_funcs->server_get_unix_fd( hFile, FILE_APPEND_DATA, &fd, &needs_close, &type, NULL );

    if (!ret && (type == FD_TYPE_FILE || type == FD_TYPE_DIR))
    {
        if (fsync(fd))
            ret = FILE_GetNtStatus();

        io->u.Status    = ret;
        io->Information = 0;
    }
    else if (!ret && type == FD_TYPE_SERIAL)
    {
        ret = COMM_FlushBuffersFile( fd );
    }
    else if (ret != STATUS_ACCESS_DENIED)
    {
        struct async_irp *async;

        if (!(async = (struct async_irp *)alloc_fileio( sizeof(*async), irp_completion, hFile )))
            return STATUS_NO_MEMORY;
        async->buffer  = NULL;
        async->size    = 0;

        SERVER_START_REQ( flush )
        {
            req->async = server_async( hFile, &async->io, NULL, NULL, NULL, io );
            ret = wine_server_call( req );
            wait_handle = wine_server_ptr_handle( reply->event );
            if (wait_handle && ret != STATUS_PENDING)
            {
                io->u.Status    = ret;
                io->Information = 0;
            }
        }
        SERVER_END_REQ;

        if (ret != STATUS_PENDING) RtlFreeHeap( GetProcessHeap(), 0, async );

        if (wait_handle) ret = wait_async( wait_handle, FALSE, io );
    }

    if (needs_close) close( fd );
    return ret;
}

/******************************************************************
 *		NtLockFile       (NTDLL.@)
 *
 *
 */
NTSTATUS WINAPI NtLockFile( HANDLE hFile, HANDLE lock_granted_event,
                            PIO_APC_ROUTINE apc, void* apc_user,
                            PIO_STATUS_BLOCK io_status, PLARGE_INTEGER offset,
                            PLARGE_INTEGER count, ULONG* key, BOOLEAN dont_wait,
                            BOOLEAN exclusive )
{
    NTSTATUS    ret;
    HANDLE      handle;
    BOOLEAN     async;
    static BOOLEAN     warn = TRUE;

    if (apc || io_status || key)
    {
        FIXME("Unimplemented yet parameter\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    if (apc_user && warn)
    {
        FIXME("I/O completion on lock not implemented yet\n");
        warn = FALSE;
    }

    for (;;)
    {
        SERVER_START_REQ( lock_file )
        {
            req->handle      = wine_server_obj_handle( hFile );
            req->offset      = offset->QuadPart;
            req->count       = count->QuadPart;
            req->shared      = !exclusive;
            req->wait        = !dont_wait;
            ret = wine_server_call( req );
            handle = wine_server_ptr_handle( reply->handle );
            async  = reply->overlapped;
        }
        SERVER_END_REQ;
        if (ret != STATUS_PENDING)
        {
            if (!ret && lock_granted_event) NtSetEvent(lock_granted_event, NULL);
            return ret;
        }

        if (async)
        {
            FIXME( "Async I/O lock wait not implemented, might deadlock\n" );
            if (handle) NtClose( handle );
            return STATUS_PENDING;
        }
        if (handle)
        {
            NtWaitForSingleObject( handle, FALSE, NULL );
            NtClose( handle );
        }
        else
        {
            LARGE_INTEGER time;
    
            /* Unix lock conflict, sleep a bit and retry */
            time.QuadPart = 100 * (ULONGLONG)10000;
            time.QuadPart = -time.QuadPart;
            NtDelayExecution( FALSE, &time );
        }
    }
}


/******************************************************************
 *		NtUnlockFile    (NTDLL.@)
 *
 *
 */
NTSTATUS WINAPI NtUnlockFile( HANDLE hFile, PIO_STATUS_BLOCK io_status,
                              PLARGE_INTEGER offset, PLARGE_INTEGER count,
                              PULONG key )
{
    NTSTATUS status;

    TRACE( "%p %x%08x %x%08x\n",
           hFile, offset->u.HighPart, offset->u.LowPart, count->u.HighPart, count->u.LowPart );

    if (io_status || key)
    {
        FIXME("Unimplemented yet parameter\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    SERVER_START_REQ( unlock_file )
    {
        req->handle = wine_server_obj_handle( hFile );
        req->offset = offset->QuadPart;
        req->count  = count->QuadPart;
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

/******************************************************************
 *		NtCreateNamedPipeFile    (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateNamedPipeFile( PHANDLE handle, ULONG access,
                                       POBJECT_ATTRIBUTES attr, PIO_STATUS_BLOCK iosb,
                                       ULONG sharing, ULONG dispo, ULONG options,
                                       ULONG pipe_type, ULONG read_mode,
                                       ULONG completion_mode, ULONG max_inst,
                                       ULONG inbound_quota, ULONG outbound_quota,
                                       PLARGE_INTEGER timeout)
{
    return unix_funcs->NtCreateNamedPipeFile( handle, access, attr, iosb, sharing, dispo, options,
                                              pipe_type, read_mode, completion_mode, max_inst,
                                              inbound_quota, outbound_quota, timeout );
}

/******************************************************************
 *		NtDeleteFile    (NTDLL.@)
 */
NTSTATUS WINAPI NtDeleteFile( OBJECT_ATTRIBUTES *attr )
{
    return unix_funcs->NtDeleteFile( attr );
}

/******************************************************************
 *		NtCancelIoFileEx    (NTDLL.@)
 *
 *
 */
NTSTATUS WINAPI NtCancelIoFileEx( HANDLE hFile, PIO_STATUS_BLOCK iosb, PIO_STATUS_BLOCK io_status )
{
    TRACE("%p %p %p\n", hFile, iosb, io_status );

    SERVER_START_REQ( cancel_async )
    {
        req->handle      = wine_server_obj_handle( hFile );
        req->iosb        = wine_server_client_ptr( iosb );
        req->only_thread = FALSE;
        io_status->u.Status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return io_status->u.Status;
}

/******************************************************************
 *		NtCancelIoFile    (NTDLL.@)
 *
 *
 */
NTSTATUS WINAPI NtCancelIoFile( HANDLE hFile, PIO_STATUS_BLOCK io_status )
{
    TRACE("%p %p\n", hFile, io_status );

    SERVER_START_REQ( cancel_async )
    {
        req->handle      = wine_server_obj_handle( hFile );
        req->iosb        = 0;
        req->only_thread = TRUE;
        io_status->u.Status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return io_status->u.Status;
}

/******************************************************************************
 *  NtCreateMailslotFile	[NTDLL.@]
 *  ZwCreateMailslotFile	[NTDLL.@]
 */
NTSTATUS WINAPI NtCreateMailslotFile( HANDLE *handle, ULONG access, OBJECT_ATTRIBUTES *attr,
                                      IO_STATUS_BLOCK *io, ULONG options, ULONG quota, ULONG msg_size,
                                      LARGE_INTEGER *timeout )
{
    return unix_funcs->NtCreateMailslotFile( handle, access, attr, io, options, quota, msg_size, timeout );
}
