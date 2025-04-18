#  Connecting OpenSearch to Microsoft Excel on Windows

## Prerequisites

* Microsoft Excel 2016 and higher
* [OpenSearch](https://opensearch.org/docs/latest/install-and-configure/install-opensearch/index/)
* [OpenSearch SQL ODBC driver](https://opensearch.org/docs/latest/search-plugins/sql/sql/odbc/)
* A preconfigured [User or System DSN](../../README.md)

## Test Successful Connection

* Open **ODBC Data Source Administrator**.
* Click on **System DSN**.
* Select **OpenSearch SQL ODBC DSN**.
* Click on **Configure**.

<img src="img/test_dsn_configure.png" width=500>

* Modify connection attributes accordingly.
* Click on **Test**.

<img src="img/test_dsn_success.png" width=300>

* You should get a message as **Connection Successful**.

## Load data 

* Open blank workbook in Microsoft Excel.
* Click on **Data** > **Get Data** > **From Other Sources** > **From ODBC**

<img src="img/excel_select_odbc.png" width="400">

* Select **OpenSearch SQL ODBC DSN**. Click **OK**.

<img src="img/excel_select_dsn.png" width="400">

* Select **Default or Custom** in connection credentials windows and click on **Connect**.

<img src="img/excel_auth.png" width="500">

* Select a table from list to load data preview. Click on **Load**.

<img src="img/excel_data_preview.png">

* Data will be loaded in the spreadsheet.

<img src="img/excel_load_data.png">

**NOTE**: There are multiple ways to load data in Microsoft Excel. Alternate options are [Data Connection Wizard](../test/data_connection_wizard.md), [Microsoft Query Connection](../test/microsoft_query_connection.md) and [Query Wizard Connection](../test/query_wizard_connection.md). These connection modes will load data relatively faster.

## Refresh Data

To refresh the data click on **Query** > **Refresh**.

<img src="img/excel_refresh.png" width=500>

Alternately, **Data** > **Refresh** option can also be used to refresh the data.

## Export as CSV files

* Click on **File** > **Save As**.
* Select Location to Save file.
* Type the file name.
* Set type as **CSV UTF-8(Comma delimited)(*.csv)**.

<img src="img/excel_export.png" width=750>

* Click **Save**.
* Data will be exported to selected location in CSV format.

## Troubleshooting

* If the table has large number of datarows, increase [the keepalive](https://github.com/opensearch-project/sql/blob/main/docs/dev/Pagination-v2.md#Cursor-Keep-Alive-Timeout) value accordlingly. 

* If the table has nested or object type column, you might get an error as below.

<img src="img/excel_data_preview_error.png" width=500>

If you ignore the error and try to load the data, column name and values might not match.

In this case, please use advanced options while connecting to the data source.

<img src="img/excel_advanced_option.png" width=500>

