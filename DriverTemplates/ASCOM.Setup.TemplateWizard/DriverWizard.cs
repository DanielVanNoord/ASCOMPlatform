﻿// Template Wizard to perform unique ASCOM subsititutions and file manipulations when creating driver templates

namespace ASCOM.Setup
{
    using System;
    using System.Collections.Generic;
    using Microsoft.VisualStudio.TemplateWizard;
    using System.Windows.Forms;
    using EnvDTE;
    using EnvDTE80;
    using System.IO;
    using System.Diagnostics;

    //using ASCOM.Internal;

    public class DriverWizard : IWizard
    {
        private DeviceDriverForm inputForm;

        // These Guids are placeholder Guids. Wherever they are used in the template project, they'll be replaced with new
        // values when the template is expanded. THE TEMPLATE PROJECTS MUST USE THESE GUIDS.
        private const string csTemplateAssemblyGuid = "28D679BA-2AF1-4557-AE15-C528C5BF91E0";
        private const string csTemplateInterfaceGuid = "3A02C211-FA08-4747-B0BD-4B00EB159297";
        private const string csTemplateRateGuid = "AD6248B3-3F51-4FFF-B62B-E3E942DD817E";
        private const string csTemplateAxisRatesGuid = "99DB28A6-0132-43BF-91C0-D723124813C8";
        private const string csTemplateTrackingRatesGuid = "49A4CA43-46B2-4D66-B9D3-FBE3ABE13DEB";

        // Private properties
        private string DeviceClass { get; set; }
        private string DeviceName { get; set; }
        private string DeviceInterface { get; set; }
        private string DeviceId { get; set; }
        private int InterfaceVersion { get; set; }
        private string Namespace { get; set; }

        private ASCOM.Utilities.TraceLogger TL = new ASCOM.Utilities.TraceLogger("TemplateWizard");
        private DTE2 myDTE;
        private ProjectItem driverTemplate;

        /// <summary>
        /// Runs custom wizard logic at the beginning of a template wizard run.
        /// </summary>
        /// <param name="automationObject"></param>
        /// <param name="replacementsDictionary"></param>
        /// <param name="runKind"></param>
        /// <param name="customParams"></param>
        public void RunStarted(object automationObject, Dictionary<string, string> replacementsDictionary, WizardRunKind runKind, object[] customParams)
        {
            TL.LogMessage("RunStarted", $"Started wizard");

            myDTE = (DTE2)automationObject;

            DialogResult dialogResult = DialogResult.Cancel;
            try
            {
                // Display a form to the user. The form collects 
                // input for the custom message.
                inputForm = new DeviceDriverForm(TL); // Pass our trace logger into the form so all Wizard trace goes into one file
                dialogResult = inputForm.ShowDialog();
            }
            catch (Exception ex)
            {
                TL.LogMessageCrLf("RunStarted", "Form Exception: " + ex.ToString());
                MessageBox.Show("Form Exception: " + ex.ToString());
            }

            if (dialogResult != DialogResult.OK) throw new WizardCancelledException("The wizard has been cancelled");

            try
            {

                // Save user inputs.
                DeviceId = inputForm.DeviceId;
                DeviceName = inputForm.DeviceName;
                DeviceClass = inputForm.DeviceClass;
                DeviceInterface = inputForm.DeviceInterface;
                InterfaceVersion = inputForm.InterfaceVersion;
                Namespace = inputForm.Namespace;
                TL.Enabled = true;
                TL.LogMessage("DeviceId", DeviceId);
                TL.LogMessage("DeviceName", DeviceName);
                TL.LogMessage("DeviceClass", DeviceClass);
                TL.LogMessage("DeviceInterface", DeviceInterface);
                TL.LogMessage("InterfaceVersion", InterfaceVersion.ToString());
                TL.LogMessage("Namespace", Namespace);

                inputForm.Dispose();
                inputForm = null;

                // Add custom parameters.
                replacementsDictionary.Add("$deviceid$", DeviceId);
                replacementsDictionary.Add("$deviceclass$", DeviceClass);
                replacementsDictionary.Add("$devicename$", DeviceName);
                replacementsDictionary.Add("$namespace$", Namespace);
                replacementsDictionary["$projectname$"] = DeviceId;
                replacementsDictionary["$safeprojectname$"] = DeviceId;
                replacementsDictionary.Add("TEMPLATEDEVICENAME", DeviceName);
                if (DeviceClass == "VideoUsingBaseClass") // Special handling for "VideoWithBaseClass" template because its file name is not the same as the device type "Video"
                {
                    replacementsDictionary.Add("TEMPLATEDEVICECLASS", "Video"); // This ensures that the class is named Video and not VideoWithBaseClass
                }
                else // ALl other templates process normally because the selected device name exatly matches the device type e.g. Telescope, Rotator etc.
                {
                    replacementsDictionary.Add("TEMPLATEDEVICECLASS", DeviceClass);
                }
                replacementsDictionary.Add("ITEMPLATEDEVICEINTERFACE", DeviceInterface);
                replacementsDictionary.Add("TEMPLATENAMESPACE", Namespace);
                replacementsDictionary.Add("TEMPLATEINTERFACEVERSION", InterfaceVersion.ToString());
                // create and replace guids
                replacementsDictionary.Add(csTemplateAssemblyGuid, Guid.NewGuid().ToString());
                replacementsDictionary.Add(csTemplateInterfaceGuid, Guid.NewGuid().ToString());
                replacementsDictionary.Add(csTemplateRateGuid, Guid.NewGuid().ToString());
                replacementsDictionary.Add(csTemplateAxisRatesGuid, Guid.NewGuid().ToString());
                replacementsDictionary.Add(csTemplateTrackingRatesGuid, Guid.NewGuid().ToString());
            }
            catch (Exception ex)
            {
                TL.LogMessageCrLf("RunStarted", "Result Exception: " + ex.ToString());
                MessageBox.Show("Form result setup exception: " + ex.ToString());
            }

        }

        /// <summary>
        /// Indicates whether the specified project item should be added to the project.
        /// </summary>
        /// <param name="filePath"></param>
        /// <returns></returns>
        /// <remarks>This method is only called for item templates, not for project templates.</remarks>
        public bool ShouldAddProjectItem(string filePath)
        {
            return true;
        }

        /// <summary>
        /// Runs custom wizard logic when a project item has finished generating.
        /// </summary>
        /// <param name="projectItem"></param>
        /// <remarks>This method is only called for item templates, not for project templates.</remarks>
        public void ProjectItemFinishedGenerating(ProjectItem projectItem)
        {
        }

        /// <summary>
        /// Runs custom wizard logic before opening an item in the template.
        /// </summary>
        /// <param name="projectItem"></param>
        /// <remarks>This method is called before opening any item that has the OpenInEditor attribute.</remarks>
        public void BeforeOpeningFile(ProjectItem projectItem)
        {
        }

        /// <summary>
        /// Runs custom wizard logic when a project has finished generating.
        /// </summary>
        /// <param name="project"></param>
        public void ProjectFinishedGenerating(Project project)
        {
            // Iterate through the project items and 
            // remove any files that begin with the word "Placeholder".
            // and the Rates class unless it's the Telescope class
            // done this way to avoid removing items from inside a foreach loop
            List<string> rems = new List<string>();
            foreach (ProjectItem item in project.ProjectItems)
            {
                if (item.Name.StartsWith("Placeholder", StringComparison.OrdinalIgnoreCase) ||
                    item.Name.StartsWith("Rate", StringComparison.OrdinalIgnoreCase) &&
                    !this.DeviceClass.Equals("Telescope", StringComparison.OrdinalIgnoreCase))
                {
                    //MessageBox.Show("adding " + item.Name);
                    rems.Add(item.Name);
                }
            }
            foreach (string item in rems)
            {
                //MessageBox.Show("Deleting " + item);
                project.ProjectItems.Item(item).Delete();
            }

            // Special handling for VB and C# driver template projects to add the interface implementation to the core driver code
            try
            {
                // Check the name of each item in the project and execute if this is a driver template project (contains Driver.vb or Driver.cs)
                foreach (ProjectItem projectItem in project.ProjectItems)
                {
                    TL.LogMessage("ProjectFinishedGenerating", "Item name: " + projectItem.Name);
                    if ((projectItem.Name.ToUpperInvariant() == "DRIVER.CS") | (projectItem.Name.ToUpperInvariant() == "DRIVER.VB"))
                    {
                        driverTemplate = projectItem; // Save the driver item
                        // This is a driver template
                        // Get the filename and directory of the Driver.xx file
                        string directory = Path.GetDirectoryName(projectItem.FileNames[1].ToString());
                        TL.LogMessage("ProjectFinishedGenerating", "File name: " + projectItem.FileNames[1].ToString() + ", Directory: " + directory);
                        TL.LogMessage("ProjectFinishedGenerating", "Found " + projectItem.Name);

                        projectItem.Open(); // Open the item for editing
                        TL.LogMessage("ProjectFinishedGenerating", "Done Open");

                        Document itemDocument = projectItem.Document; // Get the open file's document object
                        TL.LogMessage("ProjectFinishedGenerating", "Created Document");

                        itemDocument.Activate(); // Make this the current document
                        TL.LogMessage("ProjectFinishedGenerating", "Activated Document");

                        TextSelection documentSelection = (TextSelection)itemDocument.Selection; // Create a document selection
                        TL.LogMessage("ProjectFinishedGenerating", "Created Selection object");

                        const string insertionPoint = "//INTERFACECODEINSERTIONPOINT"; // Find the insertion point in the Driver.xx item
                        documentSelection.FindText(insertionPoint, (int)vsFindOptions.vsFindOptionsMatchWholeWord);
                        TL.LogMessage("ProjectFinishedGenerating", "Done INTERFACECODEINSERTIONPOINT FindText:" + documentSelection.Text);

                        // Create the name of the device interface file to be inserted
                        string insertFile = directory + "\\Device" + this.DeviceClass + Path.GetExtension(projectItem.Name);
                        TL.LogMessage("ProjectFinishedGenerating", "Opening file: " + insertFile);

                        documentSelection.InsertFromFile(insertFile); // Insert the required file at the current selection point
                        TL.LogMessage("ProjectFinishedGenerating", "Done InsertFromFile");

                        // Remove the top lines of the inserted file until we get to #Region
                        // These lines are only there to make the file error free in the template develpment project and are not required here
                        documentSelection.SelectLine(); // Select the current line
                        TL.LogMessage("ProjectFinishedGenerating", "Selected initial line: " + documentSelection.Text);
                        while (!documentSelection.Text.ToUpperInvariant().Contains("#REGION"))
                        {
                            TL.LogMessage("ProjectFinishedGenerating", "Deleting start line: " + documentSelection.Text);
                            documentSelection.Delete(); // Delete the current line
                            documentSelection.SelectLine(); // Select the new current line ready to test on the next loop 
                        }

                        // Find the end of file marker that came from the inserted file
                        const string endOfInsertFile = "//ENDOFINSERTEDFILE";
                        documentSelection.FindText(endOfInsertFile, (int)vsFindOptions.vsFindOptionsMatchWholeWord);
                        TL.LogMessage("ProjectFinishedGenerating", "Done ENDOFINSERTEDFILE FindText:" + documentSelection.Text);

                        // Delete the marker line and the last 2 lines from the inserted file
                        documentSelection.SelectLine();
                        TL.LogMessage("ProjectFinishedGenerating", "Found end line: " + documentSelection.Text);
                        while (!documentSelection.Text.ToUpperInvariant().Contains("#REGION"))
                        {
                            TL.LogMessage("ProjectFinishedGenerating", "Deleting end line: " + documentSelection.Text);
                            documentSelection.Delete(); // Delete the current line
                            documentSelection.SelectLine(); // Select the new current line ready to test on the next loop 
                            TL.LogMessage("ProjectFinishedGenerating", "Found end line: " + documentSelection.Text);
                        }

                        // Reformat the document to make it look pretty
                        documentSelection.SelectAll();
                        TL.LogMessage("ProjectFinishedGenerating", "Done SelectAll");
                        documentSelection.SmartFormat();
                        TL.LogMessage("ProjectFinishedGenerating", "Done SmartFormat");

                        itemDocument.Save(); // Save the edited file readyfor use!
                        TL.LogMessage("ProjectFinishedGenerating", "Done Save");
                        itemDocument.Close(vsSaveChanges.vsSaveChangesYes);
                        TL.LogMessage("ProjectFinishedGenerating", "Done Close");

                    }

                }

                // Iterate through the project items and remove any files that begin with the word "Device". 
                // These are the partial device implementations that are merged in to create a complete device driver template by the code above
                // They are not required in the final project

                // Done this way to avoid removing items from inside a foreach loop
                rems = new List<string>();
                foreach (ProjectItem item in project.ProjectItems)
                {
                    if (item.Name.StartsWith("Device", StringComparison.OrdinalIgnoreCase))
                    {
                        //MessageBox.Show("adding " + item.Name);
                        rems.Add(item.Name);
                    }
                }
                foreach (string item in rems)
                {
                    TL.LogMessage("ProjectFinishedGenerating", "Deleting file: " + item);
                    project.ProjectItems.Item(item).Delete();
                }

            }
            catch (Exception ex)
            {
                TL.LogMessageCrLf("ProjectFinishedGenerating Exception", ex.ToString()); // Log any error message
                MessageBox.Show(ex.ToString(), "ProjectFinishedGenerating Wizard Error", MessageBoxButtons.OK, MessageBoxIcon.Error); // Show an error message
            }

            TL.LogMessage("ProjectFinishedGenerating", "End");
            TL.Enabled = false;

        }

        /// <summary>
        /// Runs custom wizard logic when the wizard has completed all tasks.
        /// </summary>
        public void RunFinished()
        {
            try
            {
                // The interface implementation inserted in the ProjectFinishedGenerating event has its Region twistie open
                // This code is to close the interface implementation twistie so that the region appears like the common methods and support code twisties

                TL.Enabled = true;
                TL.LogMessage("RunFinished", "Start");

                // Code here commented out in order to stop the creator freezing when say a C# template is created immediately after a VB template
                // I can't figure out why this code is unreliable so I'm doing away with it because its effect is just cosmetic

                //if (driverTemplate != null) // We do have a project item to work on
                //{
                //    driverTemplate.Open(); // Open the item for editing
                //    TL.LogMessage("RunFinished", "Done Open");

                //    Document driverTemplateDocument = driverTemplate.Document; // Get the open file's document object
                //    TL.LogMessage("RunFinished", "Created Document");

                //    driverTemplateDocument.Activate(); // Make this the current document
                //    TL.LogMessage("RunFinished", "Activated Document");

                //    TextSelection selectedText = (TextSelection)driverTemplateDocument.Selection; // Create a document selection
                //    TL.LogMessage("RunFinished", "Created Selection object");

                //    selectedText.StartOfDocument(); // GO to the top of the document
                //    TL.LogMessage("RunFinished", "Done StartOfDocument Region");

                //    string pattern = "[Rr]egion \"*I" + DeviceClass; // Create a regular expression string that works for region in both VB and C#
                //    TL.LogMessage("", "RegEx search pattern: " + pattern);
                //    if (selectedText.FindText(pattern, (int)vsFindOptions.vsFindOptionsRegularExpression)) // Search for the interface implementation start of region 
                //    {
                //        // Found the interface implementation region so toggle its twistie closed
                //        selectedText.SelectLine();
                //        TL.LogMessage("RunFinished", "Found region I" + DeviceClass + " - " + selectedText.Text); // Log the line actually found
                //        myDTE.ExecuteCommand("Edit.ToggleOutliningExpansion"); // Toggle the twistie closed
                //        TL.LogMessage("RunFinished", "Done ToggleOutliningExpansion Region");
                //    }

                //    driverTemplateDocument.Close(vsSaveChanges.vsSaveChangesYes); // Save changes and close the file

                //    TL.LogMessage("RunFinished", "Done Save");
                //}
                //else // No project item so just report this (happens when a test project is being created)
                //{
                //    TL.LogMessage("RunFinished", "Project item is null, no action taken");
                //}
                TL.LogMessage("RunFinished", "End");
            }
            catch (Exception ex)
            {
                TL.LogMessageCrLf(" RunFinished Exception", ex.ToString()); // Log any error message
                MessageBox.Show(ex.ToString(), "RunFinished Wizard Error", MessageBoxButtons.OK, MessageBoxIcon.Error); // Show an error message
            }

        }

#if DEBUG
        void DumpCodeModel(Project project)
        {
            MessageBox.Show("Attach to process now", "Debug");
            ProjectItem item = project.ProjectItems.Item("Driver.cs");
            FileCodeModel fcm = item.FileCodeModel;
            CodeClass codeClass = (CodeClass2)fcm.CodeElements.Item(DeviceClass);
            // Implement the I<DeviceClass> interface on the driver class. Does not insert method stubs.
            codeClass.AddImplementedInterface("ASCOM.Interface.I" + DeviceClass, 0);
        }
#endif

    }
}