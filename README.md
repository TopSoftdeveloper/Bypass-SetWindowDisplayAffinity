# Kernel-Level Window Capture Bypass (Educational Research)

## Overview

Modern Windows applications may rely on **`SetWindowDisplayAffinity`** (`WDA_MONITOR`, `WDA_EXCLUDEFROMCAPTURE`) to prevent screenshots and screen recording.
At the **user-mode level**, these protections are relatively well understood.

This project demonstrates a **kernel-mode approach**, using a **driver-level patch**, to bypass such restrictions and allow screen capture of protected windows.

> ⚠️ **This project is for educational and research purposes only.**

---

## Technical Summary

* Operates at the **kernel / driver level**
* Bypasses window capture protection **below user-mode APIs**
* Does **not** rely on:

  * DLL injection
  * User-mode API hooking
* Demonstrates how display affinity enforcement can be neutralized from kernel space

---

## Tested Environment

| Component     | Value                    |
| ------------- | ------------------------ |
| OS            | Windows 10               |
| Version       | 22H2                     |
| Build         | 19045                    |
| Test Platform | VMware (Virtual Machine) |

✔️ Fully functional and stable in the tested virtualized environment.

---

## Notes on Real Hardware Usage

This project was validated **inside a virtual machine**.

For **real Windows installations**, driver loading and kernel patching require additional setup and safeguards.

Developers should refer to the following project for **driver installation and kernel hook techniques**:

🔗 **Reference:**
[https://github.com/iPower/KasperskyHook](https://github.com/iPower/KasperskyHook)

That repository provides guidance on:

* Proper driver loading
* Kernel hook infrastructure
* Compatibility considerations for real systems

---

## Important Considerations

* Kernel-level modifications:

  * May trigger PatchGuard
  * May require test signing mode
  * Can destabilize the system if implemented incorrectly
* Behavior may vary across:

  * Windows builds
  * GPU drivers
  * Security software

This project **does not guarantee compatibility** beyond the tested configuration.

---

## Disclaimer

This repository is published **strictly for educational, research, and defensive security analysis**.

The author:

* Does **not** encourage bypassing DRM, privacy protection, or security mechanisms
* Is **not responsible** for misuse, system damage, or legal consequences
* Strongly advises compliance with all applicable laws and software terms

---

## Intended Audience

* Reverse engineers
* Windows kernel researchers
* Security professionals
* Malware analysts (defensive research)
* OS internals learners

**Not intended for end users or production environments.**

---

## Contact & Support

For research discussion or support:

* **Telegram**: [@somerwork](https://t.me/somerwork)
* **Donate (BTC)**:
  `bc1q43u0n865fuxc4j2vgm4wp98xuuaawgkgq8yrf4`

## More Advanced version is released without using driver.

Just tell me.
