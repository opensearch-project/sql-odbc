// This file contains Data Connector logic
[Version = "1.0.1"]
section OpenSearchProject;

// When set to true, additional trace information will be written out to the User log.
// This should be set to false before release. Tracing is done through a call to
// Diagnostics.LogValue(). When EnableTraceOutput is set to false, the call becomes a
// no-op and simply returns the original value.
EnableTraceOutput = true;

[DataSource.Kind = "OpenSearchProject", Publish = "OpenSearchProject.Publish"]
shared OpenSearchProject.Contents = Value.ReplaceType(OpenSearchProjectImpl, OpenSearchProjectType);

// Wrapper function to provide additional UI customization.
OpenSearchProjectType = type function (
    Server as (
        type text meta [
            Documentation.FieldCaption = "Server",
            Documentation.FieldDescription = "The hostname of the OpenSearch server.",
            Documentation.SampleValues = {"localhost"}
        ]
    ),
    Port as (
        type number meta [
            Documentation.FieldCaption = "Port",
            Documentation.FieldDescription = "Port which OpenSearch server listens on.",
            Documentation.SampleValues = {9200}
        ]
    ),
    UseSSL as (
        type logical meta [
            Documentation.FieldCaption = "Use SSL",
            Documentation.FieldDescription = "Use SSL",
            Documentation.AllowedValues = {true, false}
        ]
    ),
    HostnameVerification as (
        type logical meta [
            Documentation.FieldCaption = "Certificate validation",
            Documentation.FieldDescription = "Certificate validation",
            Documentation.AllowedValues = {true, false}
        ]
    )
) as table meta [
    Documentation.Name = "OpenSearch Project"
];


OpenSearchProjectImpl = (Server as text, Port as number, UseSSL as logical, HostnameVerification as logical) as table =>
    let
        Credential = Extension.CurrentCredential(),
        AuthenticationMode = Credential[AuthenticationKind],
        // Sets connection string properties for authentication.
        CredentialConnectionString =
            if AuthenticationMode = "UsernamePassword" then
                [
                    Auth = "BASIC",
                    UID = Credential[Username],
                    PWD = Credential[Password]
                ]
            else if AuthenticationMode = "Key" then
                [
                    Auth = "AWS_SIGV4",
                    Region = Credential[Key]
                ]
            else if AuthenticationMode = "OAuth" then
                [
                    Auth = "OAUTH2",
                    JWT = Credential[access_token]
                ]
            else
                [
                    Auth = "NONE"
                ],
        // Sets connection string properties for encrypted connections.
        EncryptedConnectionString =
            if Credential[EncryptConnection] = null or Credential[EncryptConnection] = true then
                [
                    UseSSL = 1
                ]
            else
                [
                    UseSSL = 0
                ],
        // Subtract the server from the user input in case it's entered like 'http://localhost' or 'https://srv.com:100500' or 'localhost:0'
        // And build the proper string on our own
        FinalServerString =
            if UseSSL then
                "https://" & Uri.Parts(Server)[Host] & ":" & Text.From(Port)
            else
                "http://" & Uri.Parts(Server)[Host] & ":" & Text.From(Port),
        ConnectionString = [
            Driver = "OpenSearch SQL ODBC Driver",
            Host = FinalServerString,
            HostnameVerification = if HostnameVerification then 1 else 0
        ],
        SQLGetInfo = Diagnostics.LogValue(
            "SQLGetInfo_Options",
            [
                SQL_AGGREGATE_FUNCTIONS = ODBC[SQL_AF][All],
                SQL_SQL_CONFORMANCE = ODBC[SQL_SC][SQL_SC_SQL92_INTERMEDIATE]
            ]
        ),
        SQLGetTypeInfo = (types) =>
            if (EnableTraceOutput <> true) then
                types
            else
                let
                    // Outputting the entire table might be too large, and result in the value being truncated.
                    // We can output a row at a time instead with Table.TransformRows()
                    rows = Table.TransformRows(types, each Diagnostics.LogValue("SQLGetTypeInfo " & _[TYPE_NAME], _)),
                    toTable = Table.FromRecords(rows)
                in
                    Value.ReplaceType(toTable, Value.Type(types)),
        // SQLColumns is a function handler that receives the results of an ODBC call to SQLColumns().
        SQLColumns = (catalogName, schemaName, tableName, columnName, source) =>
            if (EnableTraceOutput <> true) then
                source
            else
            // the if statement conditions will force the values to evaluated/written to diagnostics
            if (
                Diagnostics.LogValue("SQLColumns.TableName", tableName) <> "***"
                and Diagnostics.LogValue("SQLColumns.ColumnName", columnName) <> "***"
            ) then
                let
                    // Outputting the entire table might be too large, and result in the value being truncated.
                    // We can output a row at a time instead with Table.TransformRows()
                    rows = Table.TransformRows(source, each Diagnostics.LogValue("SQLColumns", _)),
                    toTable = Table.FromRecords(rows)
                in
                    Value.ReplaceType(toTable, Value.Type(source))
            else
                source,
        SQLGetFunctions = Diagnostics.LogValue("SQLGetFunctions_Options", [
            SQL_API_SQLBINDPARAMETER = false
        ]),
        SqlCapabilities = Diagnostics.LogValue(
            "SqlCapabilities_Options",
            [
                SupportsTop = false,
                LimitClauseKind = LimitClauseKind.LimitOffset,
                Sql92Conformance = ODBC[SQL_SC][SQL_SC_SQL92_FULL],
                SupportsNumericLiterals = true,
                SupportsStringLiterals = true,
                SupportsOdbcDateLiterals = true,
                SupportsOdbcTimeLiterals = true,
                SupportsOdbcTimestampLiterals = true
            ]
        ),
        OdbcOptions = [
            // Do not view the tables grouped by their schema names.
            HierarchicalNavigation = false,
            // Prevents execution of native SQL statements. Extensions should set this to true.
            HideNativeQuery = true,
            // Allows upconversion of numeric types
            SoftNumbers = true,
            // Allow upconversion / resizing of numeric and string types
            TolerateConcatOverflow = true,
            // Enables connection pooling via the system ODBC manager
            ClientConnectionPooling = true,
            // These values should be set by previous steps
            SQLColumns = SQLColumns,
            SQLGetTypeInfo = SQLGetTypeInfo,
            SQLGetInfo = SQLGetInfo,
            SQLGetFunctions = SQLGetFunctions,
            SqlCapabilities = SqlCapabilities,
            OnError = OnOdbcError,
            // Connection string properties used for encrypted connections.
            CredentialConnectionString = EncryptedConnectionString
        ],
        FullConnectionString = (ConnectionString & CredentialConnectionString & EncryptedConnectionString),
        OdbcDatasource = Odbc.DataSource(FullConnectionString, OdbcOptions)
    in
        OdbcDatasource;

// Handles ODBC errors.
OnOdbcError = (errorRecord as record) =>
    let
        ErrorMessage = errorRecord[Message],
        ConnectionServer = errorRecord[Detail][DataSourcePath],
        IsDriverNotInstalled = Text.Contains(ErrorMessage, "doesn't correspond to an installed ODBC driver"),
        OdbcError = errorRecord[Detail][OdbcErrors]{0},
        OdbcErrorCode = OdbcError[NativeError],
        // Failed to connect to given host
        IsHostUnreachable = OdbcErrorCode = 202
    in
        if IsDriverNotInstalled then
            error
                Error.Record(
                    "DataSource.Error", "The OpenSearch SQL ODBC driver is not installed. Please install the driver"
                )
        else if IsHostUnreachable then
            error
                Error.Record(
                    "DataSource.Error",
                    "Couldn't reach server. Please double-check the server and auth. [" & ConnectionServer & "]"
                )
        else
            error errorRecord;

// Data Source Kind description
OpenSearchProject = [
    // Required for use with Power BI Service.

    TestConnection = (dataSourcePath) =>
        let
            json = Json.Document(dataSourcePath),
            Server = json[Server],
            Port = json[Port],
            UseSSL = json[UseSSL],
            HostnameVerification = json[HostnameVerification]

        in
            {"OpenSearchProject.Contents", Server, Port, UseSSL, HostnameVerification},

    // Authentication modes
    Authentication = [
        Implicit = [
            Label = "NONE"
        ],
        UsernamePassword = [
            Label = "BASIC"
        ],
        Key = [
            Label = "AWS_SIGV4",
            KeyLabel = "Region"
        ],
        OAuth = [
            Label = "OAUTH2",
            StartLogin = StartLogin,
            FinishLogin = FinishLogin,
            Refresh = Refresh,
            Logout = Logout
        ]
    ],

    callbackUri = "https://oauth.powerbi.com/views/oauthredirect.html",
    logout_uri = "https://login.microsoftonline.com/logout.srf",

<<<<<<< HEAD
    uri = "http://localhost:1852/realms/local-development/protocol/openid-connect/",
    AuthUrl = uri & "auth?",
    TokenUrl = uri & "token",
    LogoutUrl = uri & "logout",
    ClientId = "users",
    ClientSecret = "users-secret",
=======
    uriIdP = "<URI of IdP endpoint>",
    AuthUrl = uriIdP & "auth?",
    TokenUrl = uriIdP & "token",
    LogoutUrl = uriIdP & "logout",
    ClientId = "<User ID>",
    ClientSecret = "<User secret>",
>>>>>>> 0909b0b...  Prepare the branch for merge the fork
    Scope = "opensearch",

    // StartLogin function initiates OAuth2 authentication
    StartLogin = (resourceUrl, state, display) =>
            let
                AuthorizeUrl = AuthUrl
                & Uri.BuildQueryString(
                    [
                        client_id = ClientId,
                        scope = Scope,
                        state = state,
                        response_type = "code",
                        redirect_uri = callbackUri
                    ]
                )
            in
                [
                    LoginUri = AuthorizeUrl,
                    CallbackUri = callbackUri,
                    WindowHeight =  720,
                    WindowWidth =  720,
                    Context = null
                ],

    FinishLogin = (context, callbackUri, state) => let Parts = Uri.Parts(callbackUri)[Query] in TokenMethod(Parts[code]),

    TokenMethod = (code) =>
        let
            Response = Web.Contents(
                TokenUrl,
                [
                    Content = Text.ToBinary(
                        Uri.BuildQueryString(
                            [
                                client_id = ClientId,
                                client_secret = ClientSecret,
                                code = code,
                                grant_type = "authorization_code",
                                redirect_uri = callbackUri
                            ]
                        )
                    ),
                    Headers = [#"Content-type" = "application/x-www-form-urlencoded", #"Accept" = "application/json"]
                ]
            ),
            Parts = Json.Document(Response)
        in
            Parts,

    Refresh = (refreshToken) =>
        let
            tokenUrl = TokenUrl,
            requestBody = "grant_type=refresh_token&refresh_token=" & refreshToken & "&client_id=" & ClientId & "&client_secret=" & ClientSecret,
            refreshResponse = Json.Document(Web.Contents(tokenUrl, [Content = Text.ToBinary(requestBody), Headers = [#"Content-Type"="application/x-www-form-urlencoded"]])),
            refreshedAccessToken = refreshResponse[access_token]
        in
            refreshedAccessToken,

    Logout = (accessToken) =>
        let
            logoutUrl = LogoutUrl,
            redirectUrl = callbackUri,
            response = Web.Contents(logoutUrl, [Query = [redirect_uri = redirectUrl], Headers = [Authorization="Bearer " & accessToken]])
        in
            // Assuming successful logout
            null,

    // PBIDS Handler
    DSRHandlers = [
        opensearchproject = [
            GetDSR = (Server, Port, UseSSL, HostnameVerification, optional Options) =>
                [
                    protocol = "opensearchproject-odbc",
                    address = [
                        server = Server,
                        port = Port,
                        useSSL = UseSSL,
                        hostnameVerification = HostnameVerification
                    ]
                ],
            GetFormula = (dsr, optional options) =>
                () =>
                    let
                        db = OpenSearchProject.Contents(
                            dsr[address][server],
                            dsr[address][port],
                            dsr[address][useSSL],
                            dsr[address][hostnameVerification]
                        )
                    in
                        db,
            GetFriendlyName = (dsr) => "OpenSearch Project"
        ]
    ],
    // Enable Encryption
    SupportsEncryption = true,
    Label = Extension.LoadString("DataSourceLabel")
];

// Data Source UI publishing description
OpenSearchProject.Publish = [
    Beta = false,
    Category = "Other",
    ButtonText = {Extension.LoadString("ButtonTitle"), Extension.LoadString("ButtonHelp")},
    LearnMoreUrl = "https://github.com/opensearch-project/sql-odbc/blob/main/bi-connectors/PowerBIConnector/OpenSearchProject.md",
    SupportsDirectQuery = true,
    SourceImage = OpenSearch.Icons,
    SourceTypeImage = OpenSearch.Icons
];

OpenSearch.Icons = [
    Icon16 = {
        Extension.Contents("OpenSearch16.png"),
        Extension.Contents("OpenSearch20.png"),
        Extension.Contents("OpenSearch24.png"),
        Extension.Contents("OpenSearch32.png")
    },
    Icon32 = {
        Extension.Contents("OpenSearch32.png"),
        Extension.Contents("OpenSearch40.png"),
        Extension.Contents("OpenSearch48.png"),
        Extension.Contents("OpenSearch64.png")
    }
];

// Load common library functions
Extension.LoadFunction = (name as text) =>
    let
        binary = Extension.Contents(name), asText = Text.FromBinary(binary)
    in
        Expression.Evaluate(asText, #shared);

// Diagnostics module contains multiple functions. .
Diagnostics = Extension.LoadFunction("Diagnostics.pqm");

Diagnostics.LogValue = if (EnableTraceOutput) then Diagnostics[LogValue] else (prefix, value) => value;

// OdbcConstants contains numeric constants from the ODBC header files, and helper function to create bitfield values.
<<<<<<< HEAD
ODBC = Extension.LoadFunction("OdbcConstants.pqm");
=======
ODBC = Extension.LoadFunction("OdbcConstants.pqm");
>>>>>>> 0909b0b...  Prepare the branch for merge the fork
