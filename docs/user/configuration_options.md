# Configuration Options

>**NOTE:** All option names are *case-insensitive*.

#### Basic Options

| Option            | Description                                                       | Type   |
|-------------------|-------------------------------------------------------------------|--------|
| `DSN`             | **D**ata **S**ource **N**ame used for configuring the connection. | string |
| `Host` / `Server` | Hostname or IP address for the target cluster.                    | string |
| `Port`            | Port number on which the cluster's REST interface is listening.   | string |

#### Authentication Options

| Option             | Description                                                                   | Type   | Acceptable Values                                    | Default |
|--------------------|-------------------------------------------------------------------------------|--------|------------------------------------------------------|---------|
| `Auth`             | Authentication mechanism to use.                                              | string | `BASIC` (basic HTTP), `AWS_SIGV4` (AWS auth), `NONE` | `NONE`  |
| `User` / `UID`     | [`Auth=BASIC`] Username for the connection.                                   | string |                                                      |         |
| `Password` / `PWD` | [`Auth=BASIC`] Password for the connection.                                   | string |                                                      |         |
| `Region`           | [`Auth=AWS_SIGV4`] Region used for signing requests                           | string | AWS region (eg. `us-west-1`)                         |         |
| `TunnelHost`       | [`Auth=AWS_SIGV4`] VPC endpoint hostname if connected through tunnel or proxy | string |                                                      |         |

**NOTE:** To use `AWS_SIGV4` authentication you need to create `~/.aws/credentials` and add `opensearchodbc` profile with aws access key id, secret key and session token (if used).

#### Advanced Options

| Option                 | Description                                                                                                                              | Type    | Acceptable Values               | Default     |
|------------------------|------------------------------------------------------------------------------------------------------------------------------------------|---------|---------------------------------|-------------|
| `UseSSL`               | Whether to establish the connection over SSL/TLS                                                                                         | boolean | `0` or `1`                      | `0` (false) |
| `HostnameVerification` | Indicate whether certificate hostname verification should be performed for an SSL/TLS connection.                                        | boolean | `0` or `1`                      | `1` (true)  |
| `ResponseTimeout`      | The maximum time to wait for responses from the `Host`, in seconds.                                                                      | integer |                                 | `10`        |
| `FetchSize`            | Rows per page for cursor requests. The default (-1) and 0 don't page, so results stop at the server size limit (see note below).         | integer | `-1`, `0` or 1 to `10000`       | `-1`        |

**NOTE:** With the default `FetchSize` (-1) the driver sends no fetch size, so the result is not paged and is capped at the server's `plugins.query.size_limit` (10,000 rows by default), with no error or warning. `0` behaves the same way. To retrieve larger results, set a positive `FetchSize` such as `1000`; the driver then pages through the whole result. Values above the index's `index.max_result_window` (10,000 by default) fail with a "Result window is too large" error.

#### Logging Options

| Option      | Description                       | Type    | Acceptable Values                                                                                                                                                     | Default                                         |
|-------------|-----------------------------------|---------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------|
| `LogLevel`  | Severity level for driver logs.   | integer | `0` -- `OFF` <br /> `1` -- `FATAL` <br /> `2` -- `ERROR` <br /> `3` -- `WARNING` <br /> `4` -- `INFO` <br /> `5` -- `DEBUG` <br /> `6` -- `TRACE` <br /> `7` -- `ALL` | `3`                                             |
| `LogOutput` | Location for storing driver logs. | string  |                                                                                                                                                                       | WIN: `C:\` <br /> MAC: `/Library/ODBC/opensearch-sql-odbc` |


**NOTE:** Administrative privileges are required to change the value of logging options on Windows.
