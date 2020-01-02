Option Strict On
Option Explicit On
Imports ASCOM.Utilities.Exceptions
Imports ASCOM.Utilities.Interfaces
Imports System.IO
Imports System.Reflection
Imports System.Runtime.InteropServices

''' <summary>
''' The Chooser object provides a way for your application to let your user select the telescope to use.
''' </summary>
''' <remarks>
''' <para>Calling Chooser.Choose() causes a chooser window to appear, with a drop down selector list containing all of the registered Telescope 
''' drivers, listed by the driver's friendly/display name. The user sees a list of telescope types and selects one. 
''' Before the OK button will light up, however, the user must click the Setup button, causing the selected driver's setup dialog to appear 
''' (it calls the driver's Telescope.SetupDialog() method). When the setup dialog is closed, the OK button will light and allow the user 
''' to close the Chooser itself.</para>
''' 
''' <para>The Choose() method returns the DriverID of the selected driver. Choose() allows you to optionally pass the DriverID of a "current" 
''' driver (you probably save this in the registry), and the corresponding telescope type is pre-selected in the Chooser's list. In this case, 
''' the OK button starts out enabled (lit-up); the assumption is that the pre-selected driver has already been configured. </para>
'''</remarks>
<Guid("B7A1F5A0-71B4-44f9-91E9-468697957D6B"),
ComVisible(True),
ClassInterface(ClassInterfaceType.None)>
Public Class Chooser
    Implements IChooser, IChooserExtra, IDisposable
    '   ===========
    '   CHOOSER.CLS
    '   ===========
    '
    ' Implementation of the ASCOM telescope driver Chooser class
    '
    ' Written:  24-Aug-00   Robert B. Denny <rdenny@dc3.com>
    '
    ' Edits:
    '
    ' When      Who     What
    ' --------- ---     --------------------------------------------------
    ' 25-Feb-09 pwgs     5.1.0 - Refactored for Utilities
    '---------------------------------------------------------------------

    Private deviceTypeValue As String = ""

#Region " New and IDisposable Support "

    Private disposedValue As Boolean = False        ' To detect redundant calls

    ''' <summary>
    ''' Creates a new Chooser object
    ''' </summary>
    ''' <remarks></remarks>
    Public Sub New()
        MyBase.New()

        deviceTypeValue = "Telescope" ' Default to Telescope chooser
    End Sub



    ' IDisposable
    ''' <summary>
    ''' Does the work of cleaning up objects used by Chooser
    ''' </summary>
    ''' <param name="disposing">True if called by the user, false if called by the system</param>
    ''' <remarks>You can't call this directly, use Dispose with no arguments instead.</remarks>
    Protected Overridable Sub Dispose(ByVal disposing As Boolean)
        If Not disposedValue Then
            If disposing Then
            End If

        End If
        disposedValue = True
    End Sub

    ' This code added by Visual Basic to correctly implement the disposable pattern.
    ''' <summary>
    ''' Cleans up and disposes objects used by Chooser
    ''' </summary>
    ''' <remarks></remarks>
    Public Sub Dispose() Implements IDisposable.Dispose
        ' Do not change this code.  Put clean-up code in Dispose(ByVal disposing As Boolean) above.
        Dispose(True)
        GC.SuppressFinalize(Me)
    End Sub

    Protected Overrides Sub Finalize()
        ' Do not change this code.  Put clean-up code in Dispose(ByVal disposing As Boolean) above.
        Dispose(False)
        MyBase.Finalize()
    End Sub

#End Region

#Region "IChooser Implementation"
    ''' <summary>
    ''' The type of device for which the Chooser will select a driver. (String, default = "Telescope")
    ''' </summary>
    ''' <value>The type of device for which the Chooser will select a driver. (String, default = "Telescope") 
    '''</value>
    ''' <returns>The device type that has been set</returns>
    ''' <exception cref="Exceptions.InvalidValueException">Thrown on setting the device type to empty string</exception>
    ''' <remarks>This property changes the "personality" of the Chooser, allowing it to be used to select a driver for any arbitrary 
    ''' ASCOM device type. The default value for this is "Telescope", but it could be "Focuser", "Camera", etc. 
    ''' <para>This property is independent of the Profile object's DeviceType property. Setting Chooser's DeviceType 
    ''' property doesn't set the DeviceType property in Profile, you must set that also when needed.</para>
    '''</remarks>
    Public Property DeviceType() As String Implements IChooser.DeviceType
        Get
            Return deviceTypeValue
        End Get
        Set(ByVal Value As String)
            If Value = "" Then Throw New Exceptions.InvalidValueException("Chooser:DeviceType - " & MSG_ILLEGAL_DEVTYPE) 'Err.Raise(SCODE_ILLEGAL_DEVTYPE, ERR_SOURCE_PROFILE, MSG_ILLEGAL_DEVTYPE)

            deviceTypeValue = Value
        End Set
    End Property

    ''' <summary>
    ''' Select ASCOM driver to use including pre-selecting one in the drop-down list
    ''' </summary>
    ''' <param name="DriverProgID">Driver to preselect in the chooser dialogue</param>
    ''' <returns>Driver ID of chosen driver</returns>
    ''' <exception cref="Exceptions.InvalidValueException">Thrown if the Chooser.DeviceType property has not been set before Choose is called. 
    ''' It must be set in order for Chooser to know which list of devices to display.</exception>
    ''' <remarks>The supplied driver will be pre-selected in the Chooser's list when the chooser window is first opened.
    ''' </remarks>
    Public Overloads Function Choose(ByVal DriverProgID As String) As String Implements IChooser.Choose
        Dim selectedProgId As String
        Dim chooserFormInstance As ChooserForm

        Try

            If String.IsNullOrEmpty(deviceTypeValue) Then Throw New Exceptions.InvalidValueException("Unknown device type, DeviceType property has not been set")
            chooserFormInstance = New ChooserForm ' Initially hidden

            chooserFormInstance.DeviceType = deviceTypeValue
            chooserFormInstance.SelectedProgId = DriverProgID
            chooserFormInstance.ShowDialog() ' Display MODAL Chooser dialogue

            selectedProgId = chooserFormInstance.SelectedProgId

            chooserFormInstance.Dispose()

        Catch ex As DriverNotRegisteredException
            MsgBox("Chooser Exception: " & ex.Message)
            LogEvent("Chooser", "Exception", EventLogEntryType.Error, EventLogErrors.ChooserException, ex.ToString)
            selectedProgId = ""

        Catch ex As Exception
            MsgBox("Chooser Exception: " & ex.ToString)
            LogEvent("Chooser", "Exception", EventLogEntryType.Error, EventLogErrors.ChooserException, ex.ToString)
            selectedProgId = ""
        End Try

        Return selectedProgId
    End Function
#End Region

#Region "IChooserExtra Implementation"
    ''' <summary>
    ''' Select ASCOM driver to use without pre-selecting in the dropdown list
    ''' </summary>
    ''' <returns>Driver ID of chosen driver</returns>
    ''' <remarks>No driver will be pre-selected in the Chooser's list when the chooser window is first opened. 
    ''' <exception cref="Exceptions.InvalidValueException">Thrown if the Chooser.DeviceType property has not been set before Choose is called. 
    ''' It must be set in order for Chooser to know which list of devices to display.</exception>
    ''' <para>This overload is not available through COM, please use "Choose(ByVal DriverProgID As String)"
    ''' with an empty string parameter to achieve this effect.</para>
    ''' </remarks>
    <ComVisible(False)>
    Public Overloads Function Choose() As String Implements IChooserExtra.Choose
        Return Choose("")
    End Function
#End Region

End Class