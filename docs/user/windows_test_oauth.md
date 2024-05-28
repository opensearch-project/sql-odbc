# Test the Oauth2 connection on Windows

## Test Default Connection

1. Open ODBC Data Source Administrator.

 <img src="img/win_odbc_data_sources.png" width="30%">

**NOTE**: Use 32 bit application to configure 32 bit driver and 64 bit app for 64 bit driver.

2. Select the OpenSearch driver and click on the **Configure...** tab.

 <img src="img/odbc_setup.png" width="60%">

3. Change values of configuration options accordingly, then select `OAUTH2` and insert the token.

 <img src="img/odbc_test.png" width="50%">

4. Click on `Test` to verify connectivity. You will get a message as `Connection successful`.

 <img src="img/win_test_dsn.png" width="20%">

