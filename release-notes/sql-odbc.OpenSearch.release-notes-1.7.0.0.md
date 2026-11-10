## Version 1.7.0.0 Release Notes

Tested against OpenSearch 2.x and 3.x. Covers all changes since 1.5.0.0 (1.6.0.0 was tagged but not published).

### Security

* Honor `HostnameVerification`: TLS certificates were previously never verified, whatever the setting. Clusters with self-signed or otherwise untrusted certificates now need `HostnameVerification=0` ([#127](https://github.com/opensearch-project/sql-odbc/pull/127))
* Limit server response bodies in error messages to a short prefix; the full body is logged only at TRACE ([#119](https://github.com/opensearch-project/sql-odbc/pull/119))

### Features

* Add `FetchSize` connection option to page through results with a server cursor ([#64](https://github.com/opensearch-project/sql-odbc/pull/64))
* Add Amazon OpenSearch Serverless support ([#73](https://github.com/opensearch-project/sql-odbc/pull/73))
* Build and test the driver on Linux with unixODBC ([#110](https://github.com/opensearch-project/sql-odbc/pull/110))

### Enhancements

* Report a failed cursor page from `SQLFetch` (SQLSTATE `08S01`/`HY000`) instead of silently ending the result set ([#125](https://github.com/opensearch-project/sql-odbc/pull/125))
* Report non-JSON error responses (e.g. a proxy's HTML 502) with their HTTP status and body ([#124](https://github.com/opensearch-project/sql-odbc/pull/124))
* Update SQL conformance level ([#83](https://github.com/opensearch-project/sql-odbc/pull/83))

### Bug Fixes

* Honor `UseSSL=1` when `Host` has no `http://`/`https://` prefix ([#127](https://github.com/opensearch-project/sql-odbc/pull/127))
* Fix `SQLFetch` stopping after the first `FetchSize` rows on Linux ([#125](https://github.com/opensearch-project/sql-odbc/pull/125))
* Fix hangs when a cursor page request fails or gets no response ([#125](https://github.com/opensearch-project/sql-odbc/pull/125))
* Fix crash on non-JSON error responses ([#124](https://github.com/opensearch-project/sql-odbc/pull/124))
* Fix crash when disconnecting or running a new query while cursor pages are still being fetched ([#129](https://github.com/opensearch-project/sql-odbc/pull/129))
* Fix crash when a response can't be converted to a result set, with `SQLExecDirect` ([#130](https://github.com/opensearch-project/sql-odbc/pull/130)) and with `SQLPrepare`/`SQLExecute` ([#131](https://github.com/opensearch-project/sql-odbc/pull/131))
* Fix `float`/`double` values truncated to 6 significant digits ([#118](https://github.com/opensearch-project/sql-odbc/pull/118))
* Fix crash formatting `TIME` values with fractional seconds as text ([#121](https://github.com/opensearch-project/sql-odbc/pull/121))
* Report column aliases as column names, and the source column as `SQL_DESC_BASE_COLUMN_NAME` ([#117](https://github.com/opensearch-project/sql-odbc/pull/117))
* Map the SQL plugin's `timestamp` type name in result-set schemas ([#115](https://github.com/opensearch-project/sql-odbc/pull/115))
* Reject a character value that is not a datetime instead of returning today's date ([#105](https://github.com/opensearch-project/sql-odbc/pull/105))
* Clear the connection error after `SQLError` reports it ([#104](https://github.com/opensearch-project/sql-odbc/pull/104))
* Fix log file names on Linux and macOS ([#114](https://github.com/opensearch-project/sql-odbc/pull/114))

### Infrastructure

* Get CI building again on current runner images, with a weekly scheduled run ([#110](https://github.com/opensearch-project/sql-odbc/pull/110))
* Add E2E tests against OpenSearch 2.x and 3.x on Linux ([#116](https://github.com/opensearch-project/sql-odbc/pull/116))
* Add E2E tests for `FetchSize` pagination ([#120](https://github.com/opensearch-project/sql-odbc/pull/120))
* Add E2E tests through the unixODBC driver manager ([#128](https://github.com/opensearch-project/sql-odbc/pull/128))
* Build with warnings as errors on all platforms after fixing GCC warnings ([#122](https://github.com/opensearch-project/sql-odbc/pull/122), [#123](https://github.com/opensearch-project/sql-odbc/pull/123))
* Upload the Windows build artifacts from the Release output directories ([#112](https://github.com/opensearch-project/sql-odbc/pull/112))
* Fix `build-mac` job ([#77](https://github.com/opensearch-project/sql-odbc/pull/77), [#91](https://github.com/opensearch-project/sql-odbc/pull/91))
* Fix `linkchecker` job ([#81](https://github.com/opensearch-project/sql-odbc/pull/81))
* Pin GitHub Actions to commit SHAs ([#97](https://github.com/opensearch-project/sql-odbc/pull/97))
* Onboard the reusable backport workflow ([#99](https://github.com/opensearch-project/sql-odbc/pull/99))
* Add issues write permission to the untriaged label workflow ([#96](https://github.com/opensearch-project/sql-odbc/pull/96))
* Update the release Jenkins file and docker image ([#59](https://github.com/opensearch-project/sql-odbc/pull/59), [#60](https://github.com/opensearch-project/sql-odbc/pull/60), [#61](https://github.com/opensearch-project/sql-odbc/pull/61))

### Documentation

* Document that the default `FetchSize` returns at most the server's size limit (10,000 rows by default) ([#120](https://github.com/opensearch-project/sql-odbc/pull/120))
* Update the security contact email ([#69](https://github.com/opensearch-project/sql-odbc/pull/69))
* Point the build badge at this repository ([#106](https://github.com/opensearch-project/sql-odbc/pull/106))

### Maintenance

* Remove the LGPL-licensed Visual Leak Detector, and link unixODBC dynamically ([#110](https://github.com/opensearch-project/sql-odbc/pull/110))
* Replace unmaintained rapidjson and rabbit with nlohmann/json 3.12.0 ([#113](https://github.com/opensearch-project/sql-odbc/pull/113), [#118](https://github.com/opensearch-project/sql-odbc/pull/118))
* Bump version to 1.7.0.0
* Version updates ([#80](https://github.com/opensearch-project/sql-odbc/pull/80), [#88](https://github.com/opensearch-project/sql-odbc/pull/88))
* Maintainer updates ([#86](https://github.com/opensearch-project/sql-odbc/pull/86), [#90](https://github.com/opensearch-project/sql-odbc/pull/90), [#100](https://github.com/opensearch-project/sql-odbc/pull/100), [#107](https://github.com/opensearch-project/sql-odbc/pull/107))
