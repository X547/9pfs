#include "NinePfs.h"


// Module glue for the 9pfs file system. As in shmfs, each op is a thin lambda
// forwarding to a method on the NinePVnode / NinePVolume that owns the request.
// Only the read-only subset is wired up for now.

static status_t nine_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return B_OK;
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


static fs_vnode_ops sVnodeOps = {
	.lookup = [](fs_volume *volume, fs_vnode *dir, const char *name, ino_t *id) {
		return static_cast<NinePVnode*>(dir->private_node)->Lookup(name, *id);
	},
	.get_vnode_name = [](fs_volume *volume, fs_vnode *vnode, char *buffer, size_t bufferSize) {
		return static_cast<NinePVnode*>(vnode->private_node)->GetVnodeName(buffer, bufferSize);
	},
	.put_vnode = [](fs_volume *volume, fs_vnode *vnode, bool reenter) {
		static_cast<NinePVolume*>(volume->private_volume)->PutVnode(
			static_cast<NinePVnode*>(vnode->private_node));
		return (status_t)B_OK;
	},
	.remove_vnode = [](fs_volume *volume, fs_vnode *vnode, bool reenter) {
		static_cast<NinePVolume*>(volume->private_volume)->RemoveVnode(
			static_cast<NinePVnode*>(vnode->private_node));
		return (status_t)B_OK;
	},

	.read_pages = [](fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos, const struct iovec *vecs, size_t count, size_t *numBytes) {
		return static_cast<NinePFileVnode*>(vnode->private_node)->ReadPages(pos, vecs, count, *numBytes);
	},
	.write_pages = [](fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos, const struct iovec *vecs, size_t count, size_t *numBytes) {
		return static_cast<NinePFileVnode*>(vnode->private_node)->WritePages(pos, vecs, count, *numBytes);
	},
	.fsync = [](fs_volume *volume, fs_vnode *vnode, bool dataOnly) {
		return static_cast<NinePVnode*>(vnode->private_node)->Fsync();
	},

	.read_symlink = [](fs_volume *volume, fs_vnode *link, char *buffer, size_t *bufferSize) {
		return static_cast<NinePVnode*>(link->private_node)->ReadSymlink(buffer, *bufferSize);
	},
	.create_symlink = [](fs_volume *volume, fs_vnode *dir, const char *name, const char *path, int mode) {
		return static_cast<NinePVnode*>(dir->private_node)->CreateSymlink(name, path, mode);
	},
	.unlink = [](fs_volume *volume, fs_vnode *dir, const char *name) {
		return static_cast<NinePVnode*>(dir->private_node)->Unlink(name);
	},
	.rename = [](fs_volume *volume, fs_vnode *fromDir, const char *fromName, fs_vnode *toDir, const char *toName) {
		return static_cast<NinePVnode*>(fromDir->private_node)->Rename(fromName, static_cast<NinePVnode*>(toDir->private_node), toName);
	},
	.access = [](fs_volume *volume, fs_vnode *vnode, int mode) {
		return static_cast<NinePVnode*>(vnode->private_node)->Access(mode);
	},
	.read_stat = [](fs_volume *volume, fs_vnode *vnode, struct stat *stat) {
		return static_cast<NinePVnode*>(vnode->private_node)->ReadStat(*stat);
	},
	.write_stat = [](fs_volume *volume, fs_vnode *vnode, const struct stat *stat, uint32 statMask) {
		return static_cast<NinePVnode*>(vnode->private_node)->WriteStat(*stat, statMask);
	},

	.create = [](fs_volume *volume, fs_vnode *dir, const char *name, int openMode, int perms, void **cookie, ino_t *newVnodeID) {
		return static_cast<NinePVnode*>(dir->private_node)->Create(name, openMode, perms, *(NinePFileCookie**)cookie, *newVnodeID);
	},
	.open = [](fs_volume *volume, fs_vnode *vnode, int openMode, void **cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->Open(openMode, *(NinePFileCookie**)cookie);
	},
	.close = [](fs_volume *volume, fs_vnode *vnode, void *cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->Close((NinePFileCookie*)cookie);
	},
	.free_cookie = [](fs_volume *volume, fs_vnode *vnode, void *cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->FreeCookie((NinePFileCookie*)cookie);
	},
	.read = [](fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos, void *buffer, size_t *length) {
		return static_cast<NinePVnode*>(vnode->private_node)->Read((NinePFileCookie*)cookie, pos, buffer, *length);
	},
	.write = [](fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos, const void *buffer, size_t *length) {
		return static_cast<NinePVnode*>(vnode->private_node)->Write((NinePFileCookie*)cookie, pos, buffer, *length);
	},

	.create_dir = [](fs_volume *volume, fs_vnode *parent, const char *name, int perms) {
		return static_cast<NinePVnode*>(parent->private_node)->CreateDir(name, perms);
	},
	.remove_dir = [](fs_volume *volume, fs_vnode *parent, const char *name) {
		return static_cast<NinePVnode*>(parent->private_node)->RemoveDir(name);
	},

	.open_dir = [](fs_volume *volume, fs_vnode *vnode, void **cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->OpenDir(*(NinePDirCookie**)cookie);
	},
	.close_dir = [](fs_volume *volume, fs_vnode *vnode, void *cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->CloseDir((NinePDirCookie*)cookie);
	},
	.free_dir_cookie = [](fs_volume *volume, fs_vnode *vnode, void *cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->FreeDirCookie((NinePDirCookie*)cookie);
	},
	.read_dir = [](fs_volume *volume, fs_vnode *vnode, void *cookie, struct dirent *buffer, size_t bufferSize, uint32 *num) {
		return static_cast<NinePVnode*>(vnode->private_node)->ReadDir((NinePDirCookie*)cookie, buffer, bufferSize, *num);
	},
	.rewind_dir = [](fs_volume *volume, fs_vnode *vnode, void *cookie) {
		return static_cast<NinePVnode*>(vnode->private_node)->RewindDir((NinePDirCookie*)cookie);
	},
};


static fs_volume_ops sVolumeOps = {
	.unmount = [](fs_volume *volume) {
		return static_cast<NinePVolume*>(volume->private_volume)->Unmount();
	},
	.read_fs_info = [](fs_volume *volume, struct fs_info *info) {
		return static_cast<NinePVolume*>(volume->private_volume)->ReadFsInfo(*info);
	},
	.get_vnode = [](fs_volume *volume, ino_t id, fs_vnode *vnode, int *type, uint32 *flags, bool reenter) {
		vnode->ops = &sVnodeOps;
		return static_cast<NinePVolume*>(volume->private_volume)->GetVnode(id, *(NinePVnode**)&vnode->private_node, *type, *flags, reenter);
	},
};


static file_system_module_info sModule = {
	.info = {
		.name = "file_systems/9pfs" B_CURRENT_FS_API_VERSION,
		.std_ops = nine_std_ops,
	},
	.short_name = "9pfs",
	.pretty_name = "9P (Plan 9) File System",
	.flags = 0,
	.mount = [](fs_volume *volume, const char *device, uint32 flags, const char *args, ino_t *rootVnodeID) {
		volume->ops = &sVolumeOps;
		return NinePVolume::Mount(*(NinePVolume**)&volume->private_volume, volume, device, flags, args, *rootVnodeID);
	},
};

_EXPORT module_info *modules[] = {
	(module_info*)&sModule,
	NULL
};
