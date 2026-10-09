#include "pcanBasicClass.h"
#include "../communication/PeakLibrary.h"

PCANBasicClass::PCANBasicClass(bool loadApi)
{
    //Init members
    //
    m_bWasLoaded = false;
    m_hDll = NULL;

    // Loads the API
    //
    InitializePointers();
    if (loadApi) LoadAPI();
}

PCANBasicClass::~PCANBasicClass()
{
    // Unloads the API
    //
    UnloadAPI();
}

void PCANBasicClass::LoadAPI()
{
    // Initializes pointers
    //
    InitializePointers();

    // Loads the DLL
    //
    if(!LoadDllHandle())
        return;

    // Loads API functions
    //
    m_pInitialize = (fpCANInitialize)GetFunction("CAN_Initialize");
    m_pInitializeFD = (fpCANInitializeFD)GetFunction("CAN_InitializeFD");
    m_pUnInitialize = (fpCANUninitialize)GetFunction("CAN_Uninitialize");
    m_pReset = (fpCANReset)GetFunction("CAN_Reset");
    m_pGetStatus = (fpCANGetStatus)GetFunction("CAN_GetStatus");
    m_pRead = (fpCANRead)GetFunction("CAN_Read");
    m_pReadFD = (fpCANReadFD)GetFunction("CAN_ReadFD");
    m_pWrite = (fpCANWrite)GetFunction("CAN_Write");
    m_pWriteFD = (fpCANWriteFD)GetFunction("CAN_WriteFD");
    m_pFilterMessages = (fpCANFilterMessages)GetFunction("CAN_FilterMessages");
    m_pGetValue = (fpCANGetValue)GetFunction("CAN_GetValue");
    m_pSetValue = (fpCANSetValue)GetFunction("CAN_SetValue");
    m_pGetTextError = (fpCANGetErrorText)GetFunction("CAN_GetErrorText");
    m_pLookUpChannel = (fpCANLookUpChannel)GetFunction("CAN_LookUpChannel");

    m_bWasLoaded = m_pInitialize && m_pInitializeFD && m_pUnInitialize && m_pReset &&
        m_pGetStatus && m_pRead && m_pReadFD && m_pWrite && m_pWriteFD && m_pFilterMessages &&
        m_pGetValue && m_pSetValue && m_pGetTextError && m_pLookUpChannel;

    // If the API was not loaded (Wrong version), an error message is shown.
    //
    if (!m_bWasLoaded)
    {
        m_libraryError = ERROR_PROC_NOT_FOUND;
    }
}

void PCANBasicClass::UnloadAPI()
{
    // Frees a loaded DLL
    //
    if (m_hDll != NULL)
        FreeLibrary(m_hDll);
    m_hDll = NULL;

    // Initializes pointers
    //
    InitializePointers();

    m_bWasLoaded = false;
}

void PCANBasicClass::InitializePointers()
{
    // Initializes thepointers for the PCANBasic functions
    //
    m_pInitialize = NULL;
    m_pInitializeFD = NULL;
    m_pUnInitialize = NULL;
    m_pReset = NULL;
    m_pGetStatus = NULL;
    m_pRead = NULL;
    m_pReadFD = NULL;
    m_pWrite = NULL;
    m_pWriteFD = NULL;
    m_pFilterMessages = NULL;
    m_pGetValue = NULL;
    m_pSetValue = NULL;
    m_pGetTextError = NULL;
    m_pLookUpChannel = NULL;
}

bool PCANBasicClass::LoadDllHandle()
{
    // Was already loaded
    //
    if (m_bWasLoaded)
        return true;

    //Loads Dll
    //
    m_hDll = communication::loadPeakLibrary(QStringLiteral("PCANBasic.dll"), m_libraryPath, m_libraryError);

    // Return true if the DLL was loaded or false otherwise
    //
    return (m_hDll != NULL);
}

FARPROC PCANBasicClass::GetFunction(const char* strName)
{
    // There is no DLL loaded
    //
    if (m_hDll  == NULL)
        return NULL;

    // Gets the address of the given function in the loeaded DLL
    //
    return GetProcAddress(m_hDll, strName);
}

TPCANStatus PCANBasicClass::Initialize(
        TPCANHandle Channel,
        TPCANBaudrate Btr0Btr1,
        TPCANType HwType,
        DWORD IOPort,
        WORD Interrupt)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pInitialize(Channel, Btr0Btr1, HwType, IOPort, Interrupt);
}

TPCANStatus PCANBasicClass::InitializeFD(
    TPCANHandle Channel,
    TPCANBitrateFD BitrateFD)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pInitializeFD(Channel, BitrateFD);
}

TPCANStatus PCANBasicClass::Uninitialize(
        TPCANHandle Channel)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pUnInitialize(Channel);
}

TPCANStatus PCANBasicClass::Reset(
     TPCANHandle Channel)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pReset(Channel);
}

TPCANStatus PCANBasicClass::GetStatus(
        TPCANHandle Channel)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pGetStatus(Channel);
}

TPCANStatus PCANBasicClass::Read(
        TPCANHandle Channel,
        TPCANMsg* MessageBuffer,
        TPCANTimestamp* TimestampBuffer)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pRead(Channel, MessageBuffer, TimestampBuffer);
}

TPCANStatus PCANBasicClass::ReadFD(
    TPCANHandle Channel,
    TPCANMsgFD*
    MessageBuffer,
    TPCANTimestampFD *TimestampBuffer)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pReadFD(Channel, MessageBuffer, TimestampBuffer);
}

TPCANStatus PCANBasicClass::Write(
        TPCANHandle Channel,
        TPCANMsg* MessageBuffer)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pWrite(Channel, MessageBuffer);
}

TPCANStatus PCANBasicClass::WriteFD(
    TPCANHandle Channel,
    TPCANMsgFD* MessageBuffer)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pWriteFD(Channel, MessageBuffer);
}

TPCANStatus PCANBasicClass::FilterMessages(
        TPCANHandle Channel,
        DWORD FromID,
        DWORD ToID,
        TPCANMode Mode)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pFilterMessages(Channel, FromID, ToID, Mode);
}

TPCANStatus PCANBasicClass::GetValue(
        TPCANHandle Channel,
        TPCANParameter Parameter,
        void* Buffer,
        DWORD BufferLength)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pGetValue(Channel, Parameter, Buffer, BufferLength);
}

TPCANStatus PCANBasicClass::SetValue(
        TPCANHandle Channel,
        TPCANParameter Parameter,
        void* Buffer,
        DWORD BufferLength)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pSetValue(Channel, Parameter, Buffer, BufferLength);
}

TPCANStatus PCANBasicClass::GetErrorText(
        TPCANStatus Error,
        WORD Language,
        LPSTR Buffer)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pGetTextError(Error, Language, Buffer);
}

TPCANStatus PCANBasicClass::LookUpChannel(
    LPSTR Parameters,
    TPCANHandle* FoundChannel)
{
    if (!m_bWasLoaded)
        return PCAN_ERROR_UNKNOWN;

    return (TPCANStatus)m_pLookUpChannel(Parameters, FoundChannel);
}
