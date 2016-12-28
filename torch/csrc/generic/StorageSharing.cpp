#ifdef WITH_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#endif


PyObject * THPStorage_(_sharedDecref)(THPStorage *self)
{
  HANDLE_TH_ERRORS
#ifndef THC_GENERIC_FILE
  libshm_context *ctx = NULL;
  THStorage *storage = self->cdata;
  if (storage->allocator == &THManagedSharedAllocator) {
    ctx = (libshm_context*)storage->allocatorContext;
  } else if (storage->allocator == &THStorageWeakRefAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    if (allocator_obj->allocator == &THManagedSharedAllocator)
      ctx = (libshm_context*)allocator_obj->allocatorContext;
  }
  if (ctx)
    THRefcountedMapAllocator_decref(ctx->th_context, storage->data);
#endif
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject * THPStorage_(_sharedIncref)(THPStorage *self)
{
  HANDLE_TH_ERRORS
#ifndef THC_GENERIC_FILE
  libshm_context *ctx = NULL;
  THStorage *storage = self->cdata;
  if (storage->allocator == &THManagedSharedAllocator) {
    ctx = (libshm_context*)storage->allocatorContext;
  } else if (storage->allocator == &THStorageWeakRefAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    if (allocator_obj->allocator == &THManagedSharedAllocator)
      ctx = (libshm_context*)allocator_obj->allocatorContext;
  }
  if (ctx)
    THRefcountedMapAllocator_incref(ctx->th_context, storage->data);
#endif
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

#ifndef THC_GENERIC_FILE
// TODO: move this somewhere - we only need one version
static std::string THPStorage_(__newHandle)() {
  std::string handle = "/torch_";
  handle += std::to_string(getpid());
  handle += "_";
  handle += std::to_string(THRandom_random(THPDefaultGenerator->cdata));
  return handle;
}

static PyObject * THPStorage_(_share_filename)(THPStorage *self)
{
  THStorage *storage = self->cdata;
  libshm_context *ctx;
  // Storage is already in shared memory, just return a handle
  if (storage->allocator == &THManagedSharedAllocator) {
    ctx = (libshm_context*)storage->allocatorContext;
  } else if (storage->allocator == &THStorageWeakRefAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    ctx = (libshm_context*)allocator_obj->allocatorContext;
  } else {
    // TODO: retry on collision
    // TODO: free GIL - but remember to reacquire it when an exception is thrown
    std::string handle = THPStorage_(__newHandle)();
    ctx = libshm_context_new(NULL, handle.c_str(),
            TH_ALLOCATOR_MAPPED_SHAREDMEM | TH_ALLOCATOR_MAPPED_EXCLUSIVE);
    THStoragePtr new_storage = THStorage_(newWithAllocator)(storage->size,
            &THManagedSharedAllocator, (void*)ctx);
    THStorage_(copy)(new_storage, storage);
    THStorage_(swap)(storage, new_storage);
  }

  THPObjectPtr manager_handle =
    THPUtils_bytesFromString(ctx->manager_handle);
  THPObjectPtr storage_handle =
    THPUtils_bytesFromString(THMapAllocatorContext_filename(ctx->th_context));
  THPObjectPtr size = PyLong_FromLong(storage->size);

  THPObjectPtr tuple = PyTuple_New(3);
  PyTuple_SET_ITEM(tuple.get(), 0, manager_handle.release());
  PyTuple_SET_ITEM(tuple.get(), 1, storage_handle.release());
  PyTuple_SET_ITEM(tuple.get(), 2, size.release());
  return tuple.release();
}

static PyObject * THPStorage_(_share_fd)(THPStorage *self)
{
  THStorage *storage = self->cdata;
  THMapAllocatorContext *ctx;
  // Storage is already in shared memory, just return a handle
  if (storage->allocator == &THMapAllocator) {
    ctx = (THMapAllocatorContext*)storage->allocatorContext;
  } else if (storage->allocator == &THStorageWeakRefAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    ctx = (THMapAllocatorContext*)allocator_obj->allocatorContext;
  } else {
    int flags = TH_ALLOCATOR_MAPPED_SHAREDMEM |
                TH_ALLOCATOR_MAPPED_EXCLUSIVE |
                TH_ALLOCATOR_MAPPED_KEEPFD |
                TH_ALLOCATOR_MAPPED_UNLINK;
    std::string handle = THPStorage_(__newHandle)();
    ctx = THMapAllocatorContext_new(handle.c_str(), flags);
    THStoragePtr new_storage = THStorage_(newWithAllocator)(storage->size,
            &THMapAllocator, (void*)ctx);
    THStorage_(copy)(new_storage, storage);
    THStorage_(swap)(storage, new_storage);
  }

  THPObjectPtr storage_handle = PyLong_FromLong(THMapAllocatorContext_fd(ctx));
  THPObjectPtr size = PyLong_FromLong(storage->size);

  THPObjectPtr tuple = PyTuple_New(2);
  PyTuple_SET_ITEM(tuple.get(), 0, storage_handle.release());
  PyTuple_SET_ITEM(tuple.get(), 1, size.release());
  return tuple.release();
}
#endif

#ifdef THC_GENERIC_FILE
static PyObject * THPStorage_(_share_cuda)(THPStorage *self)
{
  THStorage *storage = self->cdata;
  THPObjectPtr tuple = PyTuple_New(5);
  THPObjectPtr device = PyLong_FromLong(storage->device);
  THPObjectPtr _handle = Py_None;
  Py_INCREF(Py_None);
  THPObjectPtr size = PyLong_FromLong(storage->size);
  THPObjectPtr _offset = PyLong_FromLong(0);
  THPObjectPtr view_size = PyLong_FromLong(storage->size);
  if (storage->data) {
    size_t base_size;
    void *base_ptr = THCCachingAllocator_getBaseAllocation(storage->data, &base_size);
    ptrdiff_t offset = (char*)storage->data - (char*)base_ptr;

    cudaIpcMemHandle_t handle;
    THCudaCheck(cudaIpcGetMemHandle(&handle, base_ptr));

    _handle = PyBytes_FromStringAndSize((char *)&handle, CUDA_IPC_HANDLE_SIZE);
    _offset = PyLong_FromSsize_t((Py_ssize_t)offset);
    size = PyLong_FromSize_t(base_size);
  }
  if (!tuple || !device || !_handle || !size || !_offset || !view_size) {
    return NULL;
  }
  PyTuple_SET_ITEM(tuple.get(), 0, device.release());
  PyTuple_SET_ITEM(tuple.get(), 1, _handle.release());
  PyTuple_SET_ITEM(tuple.get(), 2, size.release());
  PyTuple_SET_ITEM(tuple.get(), 3, _offset.release());
  PyTuple_SET_ITEM(tuple.get(), 4, view_size.release());
  return tuple.release();
}
#endif

// Returns an object that holds a "weak" pointer to the THStorage. The
// pointer is set to None when the THStorage is deallocated. This is done by
// wrapping the storages allocator with THStorageWeakRefAllocator which is
// responsible for clearing the weak reference.
static PyObject * THPStorage_(newWeakRef)(THStorage *storage, PyObject *ctor) {
  while (storage->flag & TH_STORAGE_VIEW) {
    storage = storage->view;
  }
  bool hasWeakAllocator;
#ifdef THC_GENERIC_FILE
  hasWeakAllocator = storage->allocator == &THCStorageWeakRefAllocator;
#else
  hasWeakAllocator = storage->allocator == &THStorageWeakRefAllocator;
#endif
  if (hasWeakAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    Py_INCREF(allocator_obj->object.get());
    return allocator_obj->object.get();
  }

  THPObjectPtr args = Py_BuildValue("(N)", PyLong_FromVoidPtr(storage));
  if (!args) return NULL;
  THPObjectPtr ref = PyObject_Call(ctor, args, NULL);
  if (!ref) return NULL;
#ifdef THC_GENERIC_FILE
  storage->allocatorContext = new CudaStorageWeakRefAllocator(
        ref.get(), storage->allocator, storage->allocatorContext);
  storage->allocator = &THCStorageWeakRefAllocator;
#else
  storage->allocatorContext = new StorageWeakRefAllocator(
        ref.get(), storage->allocator, storage->allocatorContext);
  storage->allocator = &THStorageWeakRefAllocator;
#endif
  return ref.release();
}

PyObject * THPStorage_(newWithWeakPtr)(PyObject *_unused, PyObject *arg)
{
  HANDLE_TH_ERRORS
  THPObjectPtr ref = PyObject_GetAttrString(arg, "cdata");
  if (!ref) {
    return NULL;
  } else if (ref.get() == Py_None) {
    Py_RETURN_NONE;
  }
  THPUtils_assert(THPUtils_checkLong(ref.get()),
      "_new_with_weak_ptr(): arg.cdata must be an 'int'");
  THStorage *storage = (THStorage*)PyLong_AsVoidPtr(ref.get());
  // increment refcount only if it's positive
  int refcount = THAtomicGet(&storage->refcount);
  while (refcount > 0) {
    if (THAtomicCompareAndSwap(&storage->refcount, refcount, refcount + 1)) {
      return THPStorage_(New)(storage);
    }
    refcount = THAtomicGet(&storage->refcount);
  }
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject * THPStorage_(_share)(THPStorage *self, PyObject *args)
{
  HANDLE_TH_ERRORS
  PyObject *use_fd;
  PyObject *weak_ref_class;
  if (!PyArg_UnpackTuple(args, "_share", 2, 2, &use_fd, &weak_ref_class)) {
    return NULL;
  }
  THPObjectPtr res = PyTuple_New(2);
  THPObjectPtr handle;
#ifdef THC_GENERIC_FILE
  handle = THPStorage_(_share_cuda)(self);
#else
  if (use_fd == Py_False) {
    handle = THPStorage_(_share_filename)(self);
  } else {
    handle = THPStorage_(_share_fd)(self);
  }
#endif
  THPObjectPtr weak_ref = THPStorage_(newWeakRef)(self->cdata, weak_ref_class);
  if (!res || !handle || !weak_ref) {
    return NULL;
  }
  PyTuple_SET_ITEM(res.get(), 0, handle.release());
  PyTuple_SET_ITEM(res.get(), 1, weak_ref.release());
  return res.release();
  END_HANDLE_TH_ERRORS
}

#ifndef THC_GENERIC_FILE
static THStorage * THPStorage_(_newShared_filename)(PyObject *args)
{
  PyObject *_manager_handle = PyTuple_GET_ITEM(args, 0);
  PyObject *_object_handle = PyTuple_GET_ITEM(args, 1);
  PyObject *_size = PyTuple_GET_ITEM(args, 2);
  if (!THPUtils_checkBytes(_manager_handle) || !THPUtils_checkBytes(_object_handle) || !THPUtils_checkLong(_size)) {
    THPUtils_invalidArguments(args, "_new_shared in file system mode", 1, "a handle (string/bytes) and storage size (int)");
    return NULL;
  }
  const char *manager_handle = THPUtils_bytesAsString(_manager_handle);
  const char *object_handle = THPUtils_bytesAsString(_object_handle);
  long size = THPUtils_unpackLong(_size);

  libshm_context *ctx = libshm_context_new(manager_handle, object_handle,
          TH_ALLOCATOR_MAPPED_SHAREDMEM | TH_ALLOCATOR_MAPPED_NOCREATE);
  THStorage *storage = THStorage_(newWithAllocator)(size,
          &THManagedSharedAllocator, (void*)ctx);
  return storage;
}
#endif

#ifndef THC_GENERIC_FILE
static THStorage * THPStorage_(_newShared_fd)(PyObject *args)
{
  PyObject *_tmp_fd = PyTuple_GET_ITEM(args, 0);
  PyObject *_size = PyTuple_GET_ITEM(args, 1);
  if (!THPUtils_checkLong(_tmp_fd) || !THPUtils_checkLong(_size)) {
    THPUtils_invalidArguments(args, "_new_shared in file descriptor mode", 1, "a file descriptor (int) and storage size (int)");
    return NULL;
  }
  int fd;
  long tmp_fd = THPUtils_unpackLong(_tmp_fd);
  long size = THPUtils_unpackLong(_size);
  if ((fd = dup(tmp_fd)) == -1) {
    THPUtils_setError("could not duplicate a shared memory file descriptor");
    return NULL;
  }

  int flags = TH_ALLOCATOR_MAPPED_SHAREDMEM |
              TH_ALLOCATOR_MAPPED_NOCREATE |
              TH_ALLOCATOR_MAPPED_KEEPFD |
              TH_ALLOCATOR_MAPPED_FROMFD;
  THMapAllocatorContext *ctx = THMapAllocatorContext_newWithFd(NULL, fd, flags);
  THStorage *storage = THStorage_(newWithAllocator)(size, &THMapAllocator,
      (void*)ctx);
  return storage;
}
#endif

PyObject * THPStorage_(_sharedFd)(THPStorage *self)
{
  HANDLE_TH_ERRORS
  THMapAllocatorContext *ctx = NULL;
#ifndef THC_GENERIC_FILE
  THStorage *storage = self->cdata;
  if (storage->allocator == &THMapAllocator) {
    ctx = (THMapAllocatorContext*)storage->allocatorContext;
  } else if (storage->allocator == &THStorageWeakRefAllocator) {
    auto allocator_obj = ((StorageWeakRefAllocator*)storage->allocatorContext);
    if (allocator_obj->allocator == &THMapAllocator) {
      ctx = (THMapAllocatorContext*)allocator_obj->allocatorContext;
    }
  }
#endif

  THPUtils_assert(ctx, "couldn't retrieve a shared file descriptor");
  return PyLong_FromLong(THMapAllocatorContext_fd(ctx));
  END_HANDLE_TH_ERRORS
}


static THStorage * THPStorage_(_newTHView)(THStorage *base, ptrdiff_t offset, size_t size)
{
  void *data = (char*)base->data + offset;
  THStoragePtr view = THStorage_(newWithData)(LIBRARY_STATE (real*)data, size);
  view->flag = TH_STORAGE_REFCOUNTED | TH_STORAGE_VIEW;
  view->view = base;
  THStorage_(retain)(LIBRARY_STATE base);
  return view.release();
}

#ifdef THC_GENERIC_FILE
static THStorage * THPStorage_(_newShared_cuda)(PyObject *args)
{
  THPUtils_assert(PyTuple_GET_SIZE(args) == 5, "tuple of 5 items expected");
  PyObject *_device = PyTuple_GET_ITEM(args, 0);
  PyObject *_handle = PyTuple_GET_ITEM(args, 1);
  PyObject *_size = PyTuple_GET_ITEM(args, 2);
  PyObject *_offset = PyTuple_GET_ITEM(args, 3);
  PyObject *_view_size = PyTuple_GET_ITEM(args, 4);
  if (!(THPUtils_checkLong(_device) && THPUtils_checkLong(_size)
      && (_handle == Py_None || PyBytes_Check(_handle))
      && THPUtils_checkLong(_offset) && THPUtils_checkLong(_view_size))) {
    THPUtils_invalidArguments(args, "_new_shared in CUDA mode", 1,
        "(int device, bytes handle, int storage_size, int offset, int view_size");
    return NULL;
  }

  size_t storage_size = (size_t)THPUtils_unpackLong(_size);
  ptrdiff_t offset = (ptrdiff_t)THPUtils_unpackLong(_offset);
  size_t view_size =  (size_t)THPUtils_unpackLong(_view_size);

  THCPAutoGPU((int)THPUtils_unpackLong(_device));

  char *buffer;
  Py_ssize_t handle_size;
  if (PyBytes_AsStringAndSize(_handle, &buffer, &handle_size) == -1) {
    return NULL;
  }
  THPUtils_assert(handle_size == CUDA_IPC_HANDLE_SIZE, "incorrect handle size");
  cudaIpcMemHandle_t handle = *(cudaIpcMemHandle_t*)buffer;

  void *devPtr = NULL;
  THCudaCheck(cudaIpcOpenMemHandle(&devPtr, handle, cudaIpcMemLazyEnablePeerAccess));

  THStoragePtr base = THStorage_(newWithDataAndAllocator)(
      LIBRARY_STATE (real*)devPtr, storage_size, &THCIpcAllocator, NULL);
  base->flag = TH_STORAGE_REFCOUNTED | TH_STORAGE_FREEMEM;

  if (offset != 0 || view_size != storage_size) {
    THStoragePtr view = THPStorage_(_newTHView)(base.get(), offset, view_size);
    return view.release();
  }

  return base.release();
}
#endif

static PyObject * THPStorage_(_newShared)(THPStorage *self, PyObject *args)
{
  HANDLE_TH_ERRORS
  if (PyTuple_Size(args) != 2 || !PyTuple_Check(PyTuple_GET_ITEM(args, 0))) {
    THPUtils_invalidArguments(args, "_new_shared", 1, "(tuple args, type class)");
    return NULL;
  }
  PyObject *ctor_args = PyTuple_GET_ITEM(args, 0);
  PyObject *weak_ref_class = PyTuple_GET_ITEM(args, 1);
  THStorage* storage;
#ifdef THC_GENERIC_FILE
  storage = THPStorage_(_newShared_cuda)(ctor_args);
#else
  if (PyTuple_Size(ctor_args) == 3) {
    storage = THPStorage_(_newShared_filename)(ctor_args);
  } else {
    storage = THPStorage_(_newShared_fd)(ctor_args);
  }
#endif
  if (!storage) return NULL;
  THPObjectPtr tuple = PyTuple_New(2);
  THPObjectPtr pystorage = THPStorage_(New)(storage);
  THPObjectPtr ref = THPStorage_(newWeakRef)(storage, weak_ref_class);
  if (!tuple || !pystorage || !ref) {
    return NULL;
  }
  PyTuple_SET_ITEM(tuple.get(), 0, pystorage.release());
  PyTuple_SET_ITEM(tuple.get(), 1, ref.release());
  return tuple.release();
  END_HANDLE_TH_ERRORS
}

static PyObject * THPStorage_(_newView)(THPStorage *self, PyObject *args)
{
  HANDLE_TH_ERRORS
  if (PyTuple_Size(args) != 2 || !THPUtils_checkLong(PyTuple_GET_ITEM(args, 0))
      || ! THPUtils_checkLong(PyTuple_GET_ITEM(args, 1))) {
    THPUtils_invalidArguments(args, "_new_view", 1, "(int offset, int size)");
    return NULL;
  }
  long offset = THPUtils_unpackLong(PyTuple_GET_ITEM(args, 0));
  long size = THPUtils_unpackLong(PyTuple_GET_ITEM(args, 1));
  return THPStorage_(New)(THPStorage_(_newTHView)(self->cdata, offset, size));
  END_HANDLE_TH_ERRORS
}

static PyMethodDef THPStorage_(sharingMethods)[] = {
  {"_new_with_weak_ptr", (PyCFunction)THPStorage_(newWithWeakPtr), METH_O | METH_CLASS, NULL},
  {"_share", (PyCFunction)THPStorage_(_share), METH_VARARGS, NULL},
  {"_new_shared", (PyCFunction)THPStorage_(_newShared), METH_VARARGS | METH_STATIC, NULL},
  {"_new_view", (PyCFunction)THPStorage_(_newView), METH_VARARGS, NULL},
  {"_shared_decref", (PyCFunction)THPStorage_(_sharedDecref), METH_NOARGS, NULL},
  {"_shared_incref", (PyCFunction)THPStorage_(_sharedIncref), METH_NOARGS, NULL},
  {"_get_shared_fd", (PyCFunction)THPStorage_(_sharedFd), METH_NOARGS, NULL},
  {NULL}
};