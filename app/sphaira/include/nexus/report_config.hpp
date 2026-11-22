// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Release builders may supply this header from a directory outside the source
// tree. It contains an HTTPS upload endpoint only, never administrator logins.
#if __has_include("nexus_report_endpoint.hpp")
#include "nexus_report_endpoint.hpp"
#endif
#ifndef NEXUS_REPORT_UPLOAD_URL
#define NEXUS_REPORT_UPLOAD_URL ""
#endif
#ifndef NEXUS_REPORT_CATALOG_URL
#define NEXUS_REPORT_CATALOG_URL ""
#endif
namespace nexus::reports {inline constexpr char Endpoint[]=NEXUS_REPORT_UPLOAD_URL;inline constexpr char CatalogEndpoint[]=NEXUS_REPORT_CATALOG_URL;}
