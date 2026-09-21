// Minimal repro: xrRequestDisplayRefreshRateFB against a native Vulkan compositor.
//
// Creates a Vulkan (XR_KHR_vulkan_enable) OpenXR session, enumerates the display
// refresh rates, and requests the one the runtime reports. On a build where the
// in-process native compositor leaves request_display_refresh_rate unassigned,
// this calls through a null function pointer inside the runtime and crashes.
//
// The point: the refresh-rate list comes from the *system* compositor, but the
// request is dispatched to the *client* (native) compositor — so a request for a
// rate that IS in the enumerated list still reaches the unassigned slot.
//
// Build:  see CMakeLists.txt (needs Vulkan + an OpenXR loader).
// Run:    XR_RUNTIME_JSON=<runtime>.json ./refresh_rate_repro

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define XRC(call)                                                              \
    do {                                                                       \
        XrResult r = (call);                                                   \
        if (XR_FAILED(r)) { printf("FAIL %s -> %d\n", #call, (int)r); return 2; } \
    } while (0)

template <typename T> static bool proc(XrInstance i, const char* n, T* o) {
    return XR_SUCCEEDED(xrGetInstanceProcAddr(i, n, (PFN_xrVoidFunction*)o));
}

int main() {
    // ---- Instance (Vulkan + FB display refresh rate) ----
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
    auto has = [&](const char* n) {
        for (auto& e : exts) if (!strcmp(e.extensionName, n)) return true;
        return false;
    };
    if (!has(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) { printf("no XR_KHR_vulkan_enable\n"); return 2; }
    if (!has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) { printf("no XR_FB_display_refresh_rate\n"); return 2; }

    const char* enabled[] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
                             XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    strncpy(ici.applicationInfo.applicationName, "refresh_repro", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 1, 0);
    ici.enabledExtensionCount = 2;
    ici.enabledExtensionNames = enabled;
    XrInstance inst = XR_NULL_HANDLE;
    XRC(xrCreateInstance(&ici, &inst));

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    XRC(xrGetSystem(inst, &sgi, &sys));

    // ---- Vulkan (v1 path) ----
    PFN_xrGetVulkanGraphicsRequirementsKHR pReq = nullptr;
    PFN_xrGetVulkanInstanceExtensionsKHR pInstExt = nullptr;
    PFN_xrGetVulkanGraphicsDeviceKHR pDev = nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR pDevExt = nullptr;
    if (!proc(inst, "xrGetVulkanGraphicsRequirementsKHR", &pReq) ||
        !proc(inst, "xrGetVulkanInstanceExtensionsKHR", &pInstExt) ||
        !proc(inst, "xrGetVulkanGraphicsDeviceKHR", &pDev) ||
        !proc(inst, "xrGetVulkanDeviceExtensionsKHR", &pDevExt)) {
        printf("missing vulkan-enable procs\n"); return 2;
    }
    XrGraphicsRequirementsVulkanKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    XRC(pReq(inst, sys, &req));

    auto split = [](const std::string& s) {
        std::vector<std::string> out; size_t a = 0;
        for (size_t i = 0; i <= s.size(); i++)
            if (i == s.size() || s[i] == ' ' || s[i] == '\0') { if (i > a) out.push_back(s.substr(a, i - a)); a = i + 1; }
        return out;
    };
    uint32_t n = 0; pInstExt(inst, sys, 0, &n, nullptr);
    std::string instStr(n, '\0'); pInstExt(inst, sys, n, &n, instStr.data());
    std::vector<std::string> instStore = split(instStr);
    std::vector<const char*> instPtrs;
    for (auto& s : instStore) instPtrs.push_back(s.c_str());
    uint32_t avail = 0; vkEnumerateInstanceExtensionProperties(nullptr, &avail, nullptr);
    std::vector<VkExtensionProperties> ae(avail);
    vkEnumerateInstanceExtensionProperties(nullptr, &avail, ae.data());
    bool port = false;
    for (auto& e : ae) if (!strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) port = true;
    if (port) instPtrs.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo vici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vici.pApplicationInfo = &ai;
    vici.enabledExtensionCount = (uint32_t)instPtrs.size();
    vici.ppEnabledExtensionNames = instPtrs.data();
    if (port) vici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkInstance vk = VK_NULL_HANDLE;
    if (vkCreateInstance(&vici, nullptr, &vk) != VK_SUCCESS) { printf("vkCreateInstance failed\n"); return 2; }

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    XRC(pDev(inst, sys, vk, &phys));
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
    uint32_t qfi = 0; bool foundQ = false;
    for (uint32_t i = 0; i < qn; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfi = i; foundQ = true; break; }
    if (!foundQ) { printf("no graphics queue\n"); return 2; }

    n = 0; pDevExt(inst, sys, 0, &n, nullptr);
    std::string devStr(n, '\0'); pDevExt(inst, sys, n, &n, devStr.data());
    uint32_t davail = 0; vkEnumerateDeviceExtensionProperties(phys, nullptr, &davail, nullptr);
    std::vector<VkExtensionProperties> de(davail);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &davail, de.data());
    auto devHas = [&](const char* x) { for (auto& e : de) if (!strcmp(e.extensionName, x)) return true; return false; };
    std::vector<std::string> devStore;
    for (auto& s : split(devStr)) if (devHas(s.c_str())) devStore.push_back(s);
    if (devHas("VK_KHR_portability_subset")) devStore.push_back("VK_KHR_portability_subset");
    std::vector<const char*> devPtrs;
    for (auto& s : devStore) devPtrs.push_back(s.c_str());

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfi; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)devPtrs.size();
    dci.ppEnabledExtensionNames = devPtrs.data();
    VkDevice dev = VK_NULL_HANDLE;
    if (vkCreateDevice(phys, &dci, nullptr, &dev) != VK_SUCCESS) { printf("vkCreateDevice failed\n"); return 2; }

    // ---- Session ----
    XrGraphicsBindingVulkanKHR gb = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    gb.instance = vk; gb.physicalDevice = phys; gb.device = dev; gb.queueFamilyIndex = qfi; gb.queueIndex = 0;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb; sci.systemId = sys;
    XrSession session = XR_NULL_HANDLE;
    XRC(xrCreateSession(inst, &sci, &session));

    // ---- The FB refresh-rate calls ----
    PFN_xrEnumerateDisplayRefreshRatesFB pEnum = nullptr;
    PFN_xrRequestDisplayRefreshRateFB pReqRate = nullptr;
    proc(inst, "xrEnumerateDisplayRefreshRatesFB", &pEnum);
    proc(inst, "xrRequestDisplayRefreshRateFB", &pReqRate);
    if (!pEnum || !pReqRate) { printf("no FB refresh-rate procs\n"); return 2; }

    uint32_t rateCount = 0;
    XRC(pEnum(session, 0, &rateCount, nullptr));
    std::vector<float> rates(rateCount);
    if (rateCount) XRC(pEnum(session, rateCount, &rateCount, rates.data()));
    printf("xrEnumerateDisplayRefreshRatesFB -> %u rate(s):", rateCount);
    float best = 0.0f;
    for (float r : rates) { printf(" %.1f", r); if (r > best) best = r; }
    printf("\n");

    if (best <= 0.0f) { printf("no rates advertised — request would be rejected before the compositor; no repro here\n"); return 0; }

    printf("xrRequestDisplayRefreshRateFB(%.1f) ...\n", best);
    fflush(stdout);
    XrResult rr = pReqRate(session, best);   // <-- crashes here on an unguarded native compositor
    printf("xrRequestDisplayRefreshRateFB returned %d (no crash)\n", (int)rr);

    xrDestroySession(session);
    xrDestroyInstance(inst);
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(vk, nullptr);
    return 0;
}
