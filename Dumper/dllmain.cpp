#include <windows.h>
#include "jni/jni.h"
#include "jni/jni_md.h"
#include "jni/jvmti.h"
#include <ShlObj.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

#include "minhook/MinHook.h"

#pragma warning(disable: 26812)
#pragma warning(disable: 4996)

inline const char* SYSTEMDRIVE = getenv("SystemDrive"); // по дефолту это C:\\

#define AGENTMODE (std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\AGENTMODE") || std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\AGENTMODE\\"))
#define TESTMODE (std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\TESTMODE") || std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\TESTMODE\\"))

jrawMonitorID mutex;

JNIEnv* jenv;
JavaVM* jvm;

using Java_java_lang_ClassLoader_defineClass1 = jclass(__stdcall* /* я когда реверсил джаву, там было __stdcall, но почему то я думаю, что там JNICALL */)(JNIEnv* env, jobject loader, jstring name, jbyteArray data, jint offset, jint length, jobject pd, jstring source);

using Java_java_lang_ClassLoader_defineClass2 = jclass(__stdcall* /* здесь тоже самое если что */)(JNIEnv* env, jobject loader, jstring name, jobject data, jint offset, jint length, jobject pd, jstring source);

Java_java_lang_ClassLoader_defineClass1 original_defineClass1;

Java_java_lang_ClassLoader_defineClass2 original_defineClass2;

jclass JNICALL /* тут я уже по сурсам джавы чекнул, там уже JNICALL */ defineClass1_hook(JNIEnv* env, jobject loader, jstring name, jbyteArray data, jint offset, jint length, jobject pd, jstring source) {
    if (data == NULL || length < 0) {
        return original_defineClass1(env, loader, name, data, offset, length, pd, source);
    }

    jbyte* body = reinterpret_cast<jbyte*>(malloc(length));
    env->GetByteArrayRegion(data, offset, length, body);

    const char* nameChars = env->GetStringUTFChars(name, nullptr);

    std::string nameString(nameChars);

    if ((nameString.find("java") != std::string::npos) || (nameString.find("com.sun") != std::string::npos) || (nameString.find("sun") != std::string::npos))
        return original_defineClass1(env, loader, name, data, offset, length, pd, source);

    std::string path = std::filesystem::path(std::string(SYSTEMDRIVE) + "\\Dump\\").append(nameString).string();

    path += ".class";

    const char* sourceChars = env->GetStringUTFChars(source, nullptr);

    std::ofstream ofstream(path, std::ios::binary);

    if (!ofstream.is_open()) {
        std::wcout << L"Что-то пошло не так.\n";
    }

    ofstream.write(reinterpret_cast<char*>(body), length);
    ofstream.close();

    std::wcout << L"Сдампил: ";

    printf("%s\n", nameChars);

    env->ReleaseStringUTFChars(name, nameChars);
    env->ReleaseStringUTFChars(source, sourceChars);

    return original_defineClass1(env, loader, name, data, offset, length, pd, source);
}

jclass JNICALL /* тут аналогично */ defineClass2_hook(JNIEnv* env, jobject loader, jstring name, jobject data, jint offset, jint length, jobject pd, jstring source) {
    if (data == NULL || length < 0) {
        return original_defineClass2(env, loader, name, data, offset, length, pd, source);
    }

    if (!TESTMODE)
        return original_defineClass2(env, loader, name, data, offset, length, pd, source);

    jbyte* body = reinterpret_cast<jbyte*>(malloc(length));
    env->GetByteArrayRegion(reinterpret_cast<jbyteArray>(data), offset, length, body);

    const char* nameChars = env->GetStringUTFChars(name, nullptr);

    std::string nameString(nameChars);

    if ((nameString.find("java") != std::string::npos) || (nameString.find("com.sun") != std::string::npos) || (nameString.find("sun") != std::string::npos))
        return original_defineClass2(env, loader, name, data, offset, length, pd, source);

    std::string path = std::filesystem::path(std::string(SYSTEMDRIVE) + "\\Dump2\\").append(nameString).string();

    path += ".class";

    const char* sourceChars = env->GetStringUTFChars(source, nullptr);

    std::ofstream ofstream(path, std::ios::binary);

    if (!ofstream.is_open()) {
        std::wcout << L"Что-то пошло не так.\n";
    }

    ofstream.write(reinterpret_cast<char*>(body), length);
    ofstream.close();

    std::wcout << L"Сдампил: ";

    printf("%s\n", nameChars);

    env->ReleaseStringUTFChars(name, nameChars);
    env->ReleaseStringUTFChars(source, sourceChars);

    return original_defineClass2(env, loader, name, data, offset, length, pd, source);
}

void JNICALL loadClass(jvmtiEnv* jvmti, JNIEnv* env, jclass class_being_redefined, jobject loader, const char* name, jobject protection_domain, jint class_data_len, const unsigned char* class_data, jint* new_class_data_len, unsigned char** new_class_data) {
    if (AGENTMODE) {
        jvmtiError enter = jvmti->RawMonitorEnter(mutex);
        if (enter != JVMTI_ERROR_NONE) return;
        std::string str = std::string(name).replace(str.begin(), str.end(), "/", ".");

        std::string path = std::filesystem::path(std::string(SYSTEMDRIVE) + "\\Dump3\\").append(str).string();

        path += ".class";

        std::ofstream ofstream(path, std::ios::binary);

        if (!ofstream.is_open()) {
            std::wcout << L"Что-то пошло не так.\n";
        }

        ofstream.write(reinterpret_cast<const char*>(class_data) /* люблю reinterpret_cast вместо C-style, хз почему :sunglasses: */, class_data_len);
        ofstream.close();

        std::wcout << L"Сдампил: ";
        printf("%s\n", str.c_str());

        jvmtiError exit = jvmti->RawMonitorExit(mutex);
        if (exit != JVMTI_ERROR_NONE) return;
    }
}

// Сделал за 5 минут, очевидно что PoC
// может не работать, я особо не тестил
jint JNICALL AgentMode() {
    jsize count;
    if (JNI_GetCreatedJavaVMs(&jvm, 1, &count) != JNI_OK || count == 0) return 0;


    jint res = jvm->GetEnv((void**)&jenv, JNI_VERSION_1_6);

    if (res == JNI_EDETACHED) {
        res = jvm->AttachCurrentThread((void**)&jenv, nullptr);
    }

    if (res != JNI_OK) {
        return 0;
    }

    jvmtiEnv* jvmti;
    jvmtiCapabilities capabilities;
    jvmtiEventCallbacks callbacks;

    jint result = jvm->GetEnv((void**)&jvmti, JVMTI_VERSION_1);
    if (result != JNI_OK) {
        return 0;
    }

    (void)memset(&capabilities, NULL, sizeof(capabilities));
    capabilities.can_generate_all_class_hook_events = 1;

    jvmtiError error = jvmti->AddCapabilities(&capabilities);
    if (error != JVMTI_ERROR_NONE) return 0;

    (void)memset(&callbacks, NULL, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &loadClass;

    error = jvmti->SetEventCallbacks(&callbacks, (jint)sizeof(callbacks));
    if (error != JVMTI_ERROR_NONE) return 0;

    error = jvmti->SetEventNotificationMode(JVMTI_ENABLE,
        JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, reinterpret_cast<jthread>(NULL));
    if (error != JVMTI_ERROR_NONE) return 0;

    error = jvmti->CreateRawMonitor("agent data", &mutex);
    if (error != JVMTI_ERROR_NONE) return 0;

    return JNI_OK;
}

void JNICALL Dump() {
    FILE* out;
    FILE* in;
    AllocConsole();
    SetConsoleOutputCP(1251);
    setlocale(LC_ALL, "Russian");
    SetConsoleCtrlHandler(NULL, true);
    freopen_s(&out, "conout$", "w", stdout);
    freopen_s(&in, "conin$", "r", stdin);
    printf("Java Runtime Class Dumper by xWhitey.\n");
    printf(" \n");

    if (AGENTMODE && TESTMODE) {
        std::wcout << L"Обнаружены AGENTMODE и TESTMODE. Просьба удалить один, т.к оба режима не могут быть использованы одновременно.\n";
    }

    if (AGENTMODE) {
        std::wcout << L"Обнаружен AGENTMODE. Переключаю на режим дампинга через натив агент...\n";
    }

    if (TESTMODE) {
        std::wcout << L"Обнаружен TESTMODE. Включаю тестовый режим дампинга который почти ничего не дампит...\n";
    }

    if (!std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\Dump")) {
        SHCreateDirectoryExA(NULL, std::string(std::string(SYSTEMDRIVE) + "\\Dump").c_str(), NULL);
    }

    if (!std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\Dump2") && TESTMODE) {
        SHCreateDirectoryExA(NULL, std::string(std::string(SYSTEMDRIVE) + "\\Dump2").c_str(), NULL);
    }

    if (!std::filesystem::exists(std::string(SYSTEMDRIVE) + "\\Dump3") && AGENTMODE) {
        SHCreateDirectoryExA(NULL, std::string(std::string(SYSTEMDRIVE) + "\\Dump3").c_str(), NULL);
    }

    HMODULE javaDllModule = GetModuleHandleA("java.dll");
    if (!javaDllModule) {
        std::wcout << L"Не найден модуль java.dll. Ты точно в процесс джавы инжектнул?";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::exit(0);
    }

    FARPROC defineClass1Address = GetProcAddress(javaDllModule, "Java_java_lang_ClassLoader_defineClass1"); // самое крутое название функции, спасибо джава

    if (!defineClass1Address) {
        std::wcout << L"Не найден адрес функции Java_java_lang_ClassLoader_defineClass1. Ты точно инжектнул в джава процесс?\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::exit(0);
    }

    FARPROC defineClass2Address = GetProcAddress(javaDllModule, "Java_java_lang_ClassLoader_defineClass2"); // самое крутое название функции, спасибо джава х2 

    if (!defineClass2Address) {
        std::wcout << L"Не найден адрес функции Java_java_lang_ClassLoader_defineClass2. Ты точно инжектнул в джава процесс?\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::exit(0);
    }

    if (!AGENTMODE) {
        MH_Initialize();

        MH_CreateHook(defineClass1Address, defineClass1_hook, reinterpret_cast<void**>(&original_defineClass1)); // самый крутой метод дампинга :sunglasses:
        if (TESTMODE) {
            MH_CreateHook(defineClass2Address, defineClass2_hook, reinterpret_cast<void**>(&original_defineClass2));
        }
        MH_STATUS res = MH_EnableHook(MH_ALL_HOOKS);

        if (res != MH_OK) {
            std::wcout << L"Чето пошло не так при хукинге дефайнкласс, идем нахуй.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::exit(0);
        }
    } else {
        AgentMode();
    }
}

DWORD WINAPI DllMain(_In_ void* _DllHandle, _In_ unsigned long _Reason, _In_opt_ void** unused) {
    if (_Reason == DLL_PROCESS_ATTACH) {
        // вообще можно не писать этот дисейбл, ибо если инжектить через мануал маппинг этот дисейбл попросту ничего не сделает
        // но и через процесс хакер можно инжектить, который использует обычный LoadLibrary, так что оставлю этот дисейбл.
        DisableThreadLibraryCalls(reinterpret_cast<HMODULE>(_DllHandle));
        CreateThread(NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(Dump), NULL, NULL, NULL);
        return 1;
    }
    return 0;
}
