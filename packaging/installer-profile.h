#ifndef INFILTRATOR_INSTALL_PROFILE_H
#define INFILTRATOR_INSTALL_PROFILE_H

#if (defined(INSTALL_SYSTEM_MONITOR) + defined(INSTALL_CALCULATOR) + defined(INSTALL_FILES)) != 1
#error "Select exactly one provisioning product"
#elif defined(INSTALL_SYSTEM_MONITOR)
#define INSTALL_NAME "System Monitor"
#define INSTALL_SLUG "system-monitor"
#elif defined(INSTALL_CALCULATOR)
#define INSTALL_NAME "Calculator"
#define INSTALL_SLUG "calculator"
#elif defined(INSTALL_FILES)
#define INSTALL_NAME "Files"
#define INSTALL_SLUG "files"
#endif

#define STATUS_DIR "/run/infiltratoros"
#define STATUS_PATH STATUS_DIR "/" INSTALL_SLUG "-install.status"
#define LOG_PATH "/var/log/infiltratoros-" INSTALL_SLUG ".log"
#define WORKER "/usr/lib/infiltratoros/install-" INSTALL_SLUG
#define PROGRESS_UI "/usr/lib/infiltratoros/" INSTALL_SLUG "-progress"
#endif
