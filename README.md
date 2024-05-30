# OpenSearch ODBC Driver

OpenSearchODBC is a read-only ODBC driver for Windows and Mac for connecting to OpenSearch SQL support. This driver version allows to levereage on Oauth2 authentication/authorization. 

### ODBC Driver

|       |                                                 |
| ----- | ----------------------------------------------- |
| Build | [![ODBC CI][odbc-build-badge]][odbc-build-link] |

[odbc-build-badge]: https://github.com/opensearch-project/sql/actions/workflows/sql-odbc-main.yml/badge.svg
[odbc-build-link]: https://github.com/opensearch-project/sql/actions/workflows/sql-odbc-main.yml

## Specifications

The driver is compatible with ODBC 3.51.

## Supported Versions


  | Operating System  | Version                           | Supported Bitness |
  | ----------------- |-----------------------------------| ----------------- |
  |  Windows          |  Windows 10, Windows 11           | 32-bit, 64-bit    |
  |  MacOS            |  Catalina 10.15.4, Mojave 10.14.6 | 64-bit            |

## BI Tool Connectors

* [Power BI Desktop](bi-connectors/PowerBIConnector/README.md)

## Download and Installation the Driver

You can use the installers generated as part of the most recent release.
They are available for download from [Artifacts page](https://opensearch.org/artifacts) on OpenSearch.org at the very bottom and from [automated CI workflow](https://github.com/opensearch-project/sql-odbc/actions/workflows/sql-odbc-main.yml).

### Windows

1. Run the `.msi` installer to install the OpenSearch SQL ODBC Driver.
2. [Test connection](docs/user/windows_configure_dsn.md) using ODBC Data Source Administrator.

To use the driver with Tableau:
1. Copy the `.tdc` file from `<driver-install-dir>/resources` to `<windows-user-dir>/Documents/My Tableau Repository/Datasources`.

This will customize the connection from Tableau to OpenSearch, ensuring that the correct forms of queries are used. 

### Mac

[iODBC Driver Manager](http://www.iodbc.org/dataspace/doc/iodbc/wiki/iodbcWiki/WelcomeVisitors) should be installed before installing the OpenSearch SQL ODBC Driver on Mac.

1. Run the `.pkg` installer to install the OpenSearch SQL ODBC Driver.
2. Configure a Driver and DSN entry for the OpenSearch SQL ODBC Driver, following the instructions [here](docs/user/mac_configure_dsn.md).

To use the driver with Tableau:
1. Copy the `.tdc` file from `<driver-install-dir>/resources` to `<mac-user-dir>/Documents/My Tableau Repository/Datasources`.

This will customize the connection from Tableau to OpenSearch, ensuring that the correct forms of queries are used.

## Using the Driver

The driver comes in the form of a library file:
* Windows: `sqlodbc.dll`
* Mac: `libsqlodbc.dylib`

If using with ODBC compatible BI tools, refer to the tool documentation on configuring a new ODBC driver. In most cases, you will need to make the tool aware of the location of the driver library file and then use it to setup OpenSearch database connections.

### Connection Strings and Configuring the Driver

A list of options available for configuring driver behaviour is available [here](docs/user/configuration_options.md).

To setup a connection, the driver uses an ODBC connection string. Connection strings are semicolon-delimited strings specifying the set of options to use for a connection. Typically, a connection string will either:

1. specify a Data Source Name containing a pre-configured set of options (`DSN=xxx;User=xxx;Password=xxx;`)
2. or configure options explicitly using the string (`Host=xxx;Port=xxx;LogLevel=OPENSEARCH_DEBUG;...`)

## Building from source

### Building

Please refer to the [build instructions](docs/dev/BUILD_INSTRUCTIONS.md) for detailed build instructions on your platform.
If your PC is already setup to build the library, you can simply invoke cmake using

> cmake ./src

From the projects root directory, then build the project using Visual Studio (Windows) or make (Mac). 

* Visual Studio: Open **./global_make_list.sln**
* Make: Run `make` from the build root.

### Testing

**NOTE**: Some tests in ITODBCConnection will fail if a test DSN (Data Source Name) is not configured on your system. Refer to "Running Tests" in the [build instructions](docs/dev/BUILD_INSTRUCTIONS.md) for more information on configuring this.

## Documentation

Please refer to the [documentation](https://opensearch.org/docs/latest/) for detailed information on installing and configuring OpenSearch.

## Code of Conduct

This project has adopted an [Open Source Code of Conduct](CODE_OF_CONDUCT.md).

## Security issue notifications

If you discover a potential security issue in this project, notify OpenSearch Security directly by emailing security@opensearch.org. To prevent any additional risk caused by the potential issue, do **not** create a public GitHub issue.

## Licensing

See the [LICENSE](LICENSE.txt) file for our project's licensing. We will ask you to confirm the licensing of your contribution.

## Copyright

Copyright OpenSearch Contributors. See [NOTICE](NOTICE) for details.

