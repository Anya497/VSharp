#include "corProfiler.h"
#include "corhlpr.h"
#include "cComPtr.h"
#include "profiler_pal.h"
#include "logging.h"
#include "instrumenter.h"
#include "communication/protocol.h"
#include "memory/memory.h"
#include <string>

#define UNUSED(x) (void)x

using namespace icsharp;

CorProfiler::CorProfiler() : refCount(0), corProfilerInfo(nullptr), instrumenter(nullptr)
{
}

CorProfiler::~CorProfiler()
{
    if (this->corProfilerInfo != nullptr)
    {
        this->corProfilerInfo->Release();
        this->corProfilerInfo = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE CorProfiler::Initialize(IUnknown *pICorProfilerInfoUnk)
{
    HRESULT queryInterfaceResult = pICorProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo9), reinterpret_cast<void **>(&this->corProfilerInfo));

    if (FAILED(queryInterfaceResult))
    {
        return E_FAIL;
    }

    DWORD eventMask = COR_PRF_MONITOR_JIT_COMPILATION                      |
            COR_PRF_DISABLE_ALL_NGEN_IMAGES |
//            COR_PRF_DISABLE_OPTIMIZATIONS |
//            COR_PRF_MONITOR_CACHE_SEARCHES |
            COR_PRF_MONITOR_EXCEPTIONS |
            COR_PRF_MONITOR_CLR_EXCEPTIONS |
                      COR_PRF_DISABLE_TRANSPARENCY_CHECKS_UNDER_FULL_TRUST | /* helps the case where this profiler is used on Full CLR */
                      COR_PRF_DISABLE_INLINING                             ;

    // TODO: place IfFailRet here, log fails!
    auto hr = this->corProfilerInfo->SetEventMask(eventMask);

#ifdef _LOGGING
    open_log();
#endif

    auto currentThreadGetter = [=]() {
        ThreadID result;
        HRESULT hr = corProfilerInfo->GetCurrentThreadID(&result);
        if (hr != S_OK) {
            ERROR(tout << "getting current thread failed with HRESULT = " << hr);
        }
        return result;
    };
    currentThread = currentThreadGetter;

    instrumenter = new Instrumenter(*corProfilerInfo);

    protocol = new icsharp::Protocol();
    if (!protocol->connect()) return E_FAIL;
    if (!protocol->sendProbes()) return E_FAIL;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::Shutdown()
{
#ifdef _LOGGING
    close_log();
#endif

    validateEnd();

    if (this->corProfilerInfo != nullptr)
    {
        this->corProfilerInfo->Release();
        this->corProfilerInfo = nullptr;
        delete instrumenter;
    }

    if (!protocol->shutdown()) return E_FAIL;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationStarted(AppDomainID appDomainId)
{
    UNUSED(appDomainId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    UNUSED(appDomainId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownStarted(AppDomainID appDomainId)
{
    UNUSED(appDomainId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    UNUSED(appDomainId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadStarted(AssemblyID assemblyId)
{
    UNUSED(assemblyId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    UNUSED(assemblyId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadStarted(AssemblyID assemblyId)
{
    UNUSED(assemblyId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    UNUSED(assemblyId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadStarted(ModuleID moduleId)
{
    UNUSED(moduleId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    UNUSED(moduleId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadStarted(ModuleID moduleId)
{
    UNUSED(moduleId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    UNUSED(moduleId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID assemblyId)
{
    UNUSED(moduleId);
    UNUSED(assemblyId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadStarted(ClassID classId)
{
    UNUSED(classId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadFinished(ClassID classId, HRESULT hrStatus)
{
    UNUSED(classId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadStarted(ClassID classId)
{
    UNUSED(classId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadFinished(ClassID classId, HRESULT hrStatus)
{
    UNUSED(classId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FunctionUnloadStarted(FunctionID functionId)
{
    UNUSED(functionId);
    return S_OK;
}

#include<iostream>
HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock)
{
//    std::cout << __FUNCTION__ << std::endl;
    LOG(tout << __FUNCTION__ << std::endl);
    UNUSED(fIsSafeToBlock);

    return instrumenter->instrument(functionId, *protocol);
//    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
//    std::cout << __FUNCTION__ << std::endl;
    UNUSED(functionId);
    UNUSED(hrStatus);
    UNUSED(fIsSafeToBlock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchStarted(FunctionID functionId, BOOL *pbUseCachedFunction)
{
//    std::cout << __FUNCTION__ << " (method name "<< instrumenter->temp_fid_toString(functionId) << ")" << std::endl;
    std::cout << __FUNCTION__ << std::endl;
    UNUSED(functionId);
    UNUSED(pbUseCachedFunction);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchFinished(FunctionID functionId, COR_PRF_JIT_CACHE result)
{
    std::cout << __FUNCTION__ << std::endl;
    UNUSED(functionId);
    UNUSED(result);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITFunctionPitched(FunctionID functionId)
{
    std::cout << __FUNCTION__ << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITInlining(FunctionID callerId, FunctionID calleeId, BOOL *pfShouldInline)
{
    std::cout << __FUNCTION__ << std::endl;
    UNUSED(callerId);
    UNUSED(calleeId);
    UNUSED(pfShouldInline);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadCreated(ThreadID threadId)
{
    UNUSED(threadId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadDestroyed(ThreadID threadId)
{
    UNUSED(threadId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadAssignedToOSThread(ThreadID managedThreadId, DWORD osThreadId)
{
    UNUSED(managedThreadId);
    UNUSED(osThreadId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientSendingMessage(GUID *pCookie, BOOL fIsAsync)
{
    UNUSED(pCookie);
    UNUSED(fIsAsync);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientReceivingReply(GUID *pCookie, BOOL fIsAsync)
{
    UNUSED(pCookie);
    UNUSED(fIsAsync);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerReceivingMessage(GUID *pCookie, BOOL fIsAsync)
{
    UNUSED(pCookie);
    UNUSED(fIsAsync);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationReturned()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerSendingReply(GUID *pCookie, BOOL fIsAsync)
{
    UNUSED(pCookie);
    UNUSED(fIsAsync);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::UnmanagedToManagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    UNUSED(functionId);
    UNUSED(reason);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ManagedToUnmanagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    UNUSED(functionId);
    UNUSED(reason);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason)
{
    UNUSED(suspendReason);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendAborted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadSuspended(ThreadID threadId)
{
    UNUSED(threadId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadResumed(ThreadID threadId)
{
    UNUSED(threadId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    UNUSED(cMovedObjectIDRanges);
    UNUSED(oldObjectIDRangeStart);
    UNUSED(newObjectIDRangeStart);
    UNUSED(cObjectIDRangeLength);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectAllocated(ObjectID objectId, ClassID classId)
{
//    printf("Object %lu allocated!\n", objectId);
//    ULONG fieldsCount = 0;
//    ULONG bytesCount = 0;
//    HRESULT res = this->corProfilerInfo->GetClassLayout(classId, new COR_FIELD_OFFSET[0], 0, &fieldsCount, &bytesCount);
//    if (res ==  E_INVALIDARG) {
//        ULONG lengthOffset = 0;
//        ULONG contentsOffset = 0;
//        res = this->corProfilerInfo->GetStringLayout2(&lengthOffset, &contentsOffset);
//        if (res ==  E_INVALIDARG) {
//            printf("THIS IS NOT CLASS OR STRING!\n");
//        } else {
//            printf("This is string! Length offset = %u, contents offset = %u\n", lengthOffset, contentsOffset);
//            printf("Length = %d\n", *((int *)(objectId + lengthOffset)));
//            printf("Contents: ");
//            for (int i = 0; i < 100; ++i) {
//                char *p = (char*)(objectId + contentsOffset);
//                printf("%d", *p);
//            }
//            printf("\n");
//        }
//    } else {
//        mdTypeDef token;
//        ModuleID moduleId;
//        HRESULT hr;
//        IfFailRet(this->corProfilerInfo->GetClassIDInfo(classId, &moduleId, &token));

//        CComPtr<IMetaDataImport> metadataImport;
//        IfFailRet(this->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport)));

//        printf("This is class! Fields count = %u, bytes count = %u\n", fieldsCount, bytesCount);
//        COR_FIELD_OFFSET *fieldOffsets = new COR_FIELD_OFFSET[fieldsCount];
//        if (this->corProfilerInfo->GetClassLayout(classId, fieldOffsets, fieldsCount, &fieldsCount, &bytesCount) != S_OK) {
//            printf("Unexpected fail while getting class laoyut!!!");
//            return S_OK;
//        }
//        printf("Offsets:\n");
//        for (unsigned int i = 0; i < fieldsCount; ++i) {
//            printf("offset %u: token = %u, offset = %u\n", i, fieldOffsets[i].ridOfField, fieldOffsets[i].ulOffset);
//        }
//        printf("\n");
//    }
    UNUSED(objectId);
    UNUSED(classId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectsAllocatedByClass(ULONG cClassCount, ClassID classIds[], ULONG cObjects[])
{
    UNUSED(cClassCount);
    UNUSED(classIds);
    UNUSED(cObjects);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectReferences(ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[])
{
    UNUSED(objectId);
    UNUSED(classId);
    UNUSED(cObjectRefs);
    UNUSED(objectRefIds);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences(ULONG cRootRefs, ObjectID rootRefIds[])
{
    UNUSED(cRootRefs);
    UNUSED(rootRefIds);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionThrown(ObjectID thrownObjectId)
{
    HRESULT hr;
    printf("Exception %lX thrown!\n", thrownObjectId);
    ULONG size;
    ClassID classId;
    IfFailRet(corProfilerInfo->GetObjectSize(thrownObjectId, &size));
    IfFailRet(corProfilerInfo->GetClassFromObject(thrownObjectId, &classId));
    std::cout << "size of exception: " << size << std::endl;
    char *p = (char *)thrownObjectId;
    for (unsigned i = 0; i < size; ++i) {
        char b = *(p + i);
        printf("%u: %c (%hhX, %hhu)\n", i, b, b, b);
    }

    ULONG fieldsCount = 0;
    ULONG bytesCount = 0;
    IfFailRet(this->corProfilerInfo->GetClassLayout(classId, new COR_FIELD_OFFSET[0], 0, &fieldsCount, &bytesCount));
    mdTypeDef token;
    ModuleID moduleId;
    IfFailRet(this->corProfilerInfo->GetClassIDInfo(classId, &moduleId, &token));

    CComPtr<IMetaDataImport> metadataImport;
    IfFailRet(this->corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport)));

    printf("This is class! Fields count = %u, bytes count = %u\n", fieldsCount, bytesCount);
    COR_FIELD_OFFSET *fieldOffsets = new COR_FIELD_OFFSET[fieldsCount];
    if (this->corProfilerInfo->GetClassLayout(classId, fieldOffsets, fieldsCount, &fieldsCount, &bytesCount) != S_OK) {
        printf("Unexpected fail while getting class laoyut!!!");
        return S_OK;
    }
    printf("Offsets:\n");
    for (unsigned int i = 0; i < fieldsCount; ++i) {
        printf("offset %u: token = %u, offset = %u\n", i, fieldOffsets[i].ridOfField, fieldOffsets[i].ulOffset);
    }
    printf("\n");


    char spbuf[8];
    for (int i = 0; i < 8; ++i) {
        spbuf[i] = *(p + 120 + i);
    }
    char *sp = *(char **)(spbuf);
    ObjectID sid = (ObjectID) sp;
    ClassID sclassId;
    printf("string pointer: %lX\n", sid);
    unsigned ssize;
    IfFailRet(corProfilerInfo->GetObjectSize(sid, &ssize));
    IfFailRet(corProfilerInfo->GetClassFromObject(sid, &sclassId));
    std::cout << "size of string: " << ssize << std::endl;
    fflush(stdout);
    ULONG lengthOffset = 0;
    ULONG contentsOffset = 0;
    IfFailRet(this->corProfilerInfo->GetStringLayout2(&lengthOffset, &contentsOffset));
    printf("This is string! Length offset = %u, contents offset = %u\n", lengthOffset, contentsOffset);
    int length = *((int *)(sid + lengthOffset));
    printf("Length = %d\n", length);
    printf("String contents: ");
    for (int i = 0; i < 2 * length; ++i) {
        char b = *(sp + contentsOffset + i);
        printf("%u: %c (%hhX, %hhu)\n", i, b, b, b);
    }
    printf("\n");

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionEnter(FunctionID functionId)
{
    std::cout << "EXCEPTION Search function enter" << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionLeave()
{
    std::cout << "EXCEPTION Search function leave" << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterEnter(FunctionID functionId)
{
    std::cout << "EXCEPTION Search filter enter" << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterLeave()
{
    std::cout << "EXCEPTION Search filter leave" << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchCatcherFound(FunctionID functionId)
{
    std::cout << "EXCEPTION Search catcher found" << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerEnter(UINT_PTR __unused)
{
    std::cout << "EXCEPTION OS HANDLER ENTER!" << std::endl;
    UNUSED(__unused);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerLeave(UINT_PTR __unused)
{
    std::cout << "EXCEPTION OS HANDLER LEAVE!" << std::endl;
    UNUSED(__unused);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionEnter(FunctionID functionId)
{
    std::cout << "EXCEPTION UNWIND FUNCTION ENTER!" << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionLeave()
{
    std::cout << "EXCEPTION UNWIND FUNCTION LEAVE!" << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyEnter(FunctionID functionId)
{
    std::cout << "EXCEPTION UNWIND FINALLY ENTER!" << std::endl;
    UNUSED(functionId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyLeave()
{
    std::cout << "EXCEPTION UNWIND FINALLY LEAVE!" << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherEnter(FunctionID functionId, ObjectID objectId)
{
    std::cout << "EXCEPTION CATCHER ENTER!" << std::endl;
    UNUSED(functionId);
    UNUSED(objectId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherLeave()
{
    std::cout << "EXCEPTION CATCHER Leave!" << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableCreated(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable, ULONG cSlots)
{
    UNUSED(wrappedClassId);
    UNUSED(implementedIID);
    UNUSED(pVTable);
    UNUSED(cSlots);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableDestroyed(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable)
{
    UNUSED(wrappedClassId);
    UNUSED(implementedIID);
    UNUSED(pVTable);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherFound()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherExecute()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[])
{
    UNUSED(threadId);
    UNUSED(cchName);
    UNUSED(name);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionStarted(int cGenerations, BOOL generationCollected[], COR_PRF_GC_REASON reason)
{
    UNUSED(cGenerations);
    UNUSED(generationCollected);
    UNUSED(reason);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    UNUSED(cSurvivingObjectIDRanges);
    UNUSED(objectIDRangeStart);
    UNUSED(cObjectIDRangeLength);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectId)
{
    UNUSED(finalizerFlags);
    UNUSED(objectId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences2(ULONG cRootRefs, ObjectID rootRefIds[], COR_PRF_GC_ROOT_KIND rootKinds[], COR_PRF_GC_ROOT_FLAGS rootFlags[], UINT_PTR rootIds[])
{
    UNUSED(cRootRefs);
    UNUSED(rootRefIds);
    UNUSED(rootKinds);
    UNUSED(rootFlags);
    UNUSED(rootIds);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleCreated(GCHandleID handleId, ObjectID initialObjectId)
{
    UNUSED(handleId);
    UNUSED(initialObjectId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleDestroyed(GCHandleID handleId)
{
    UNUSED(handleId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::InitializeForAttach(IUnknown *pCorProfilerInfoUnk, void *pvClientData, UINT cbClientData)
{
    UNUSED(pCorProfilerInfoUnk);
    UNUSED(pvClientData);
    UNUSED(cbClientData);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerAttachComplete()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerDetachSucceeded()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock)
{
    UNUSED(functionId);
    UNUSED(rejitId);
    UNUSED(fIsSafeToBlock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl *pFunctionControl)
{
    UNUSED(moduleId);
    UNUSED(methodId);
    UNUSED(pFunctionControl);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    UNUSED(functionId);
    UNUSED(rejitId);
    UNUSED(hrStatus);
    UNUSED(fIsSafeToBlock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus)
{
    UNUSED(moduleId);
    UNUSED(methodId);
    UNUSED(functionId);
    UNUSED(hrStatus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences2(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    UNUSED(cMovedObjectIDRanges);
    UNUSED(oldObjectIDRangeStart);
    UNUSED(newObjectIDRangeStart);
    UNUSED(cObjectIDRangeLength);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences2(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    UNUSED(cSurvivingObjectIDRanges);
    UNUSED(objectIDRangeStart);
    UNUSED(cObjectIDRangeLength);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ConditionalWeakTableElementReferences(ULONG cRootRefs, ObjectID keyRefIds[], ObjectID valueRefIds[], GCHandleID rootIds[])
{
    UNUSED(cRootRefs);
    UNUSED(keyRefIds);
    UNUSED(valueRefIds);
    UNUSED(rootIds);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GetAssemblyReferences(const WCHAR *wszAssemblyPath, ICorProfilerAssemblyReferenceProvider *pAsmRefProvider)
{
    UNUSED(wszAssemblyPath);
    UNUSED(pAsmRefProvider);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleInMemorySymbolsUpdated(ModuleID moduleId)
{
    UNUSED(moduleId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::DynamicMethodJITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock, LPCBYTE ilHeader, ULONG cbILHeader)
{
    // TODO: this should be instrumented as well!!
//    printf("Dynamic Function JIT Compilation Started. %" UINT_PTR_FORMAT "\r\n", (UINT64)functionId);
    ClassID classId;
    ModuleID moduleId;
    mdToken token;
    HRESULT hr = corProfilerInfo->GetFunctionInfo(functionId, &classId, &moduleId, &token);
//    std::cout << "function info: success= " << SUCCEEDED(hr) << ", classId=" << classId << ", moduleId=" << moduleId << ", token=" << token << std::endl;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::DynamicMethodJITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
//    printf("Dynamic Function JIT Compilation Finished. %" UINT_PTR_FORMAT "\r\n", (UINT64)functionId);
    return S_OK;
}
