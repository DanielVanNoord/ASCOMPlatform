﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace ASCOM.Simulator
{
    public partial class TrafficForm : Form
    {
        private bool m_Disable = false;

        public TrafficForm()
        {
            InitializeComponent();
        }

        private void TrafficForm_FormClosed(object sender, FormClosedEventArgs e)
        {
            SharedResources.TrafficForm = null;

        }


        private void ButtonDisable_Click(object sender, EventArgs e)
        {
            m_Disable = !m_Disable;
             
            if (m_Disable) 
            {
                ButtonDisable.Text ="Enable";
            } else {
                ButtonDisable.Text ="Disable";
            }
        }
        public void TrafficLine(string message)
        {
            if (m_Disable) { return; }
            txtTraffic.Text += message + Environment.NewLine;
        }
        public void TrafficStart(string message)
        {
            if (m_Disable) { return; }
            txtTraffic.Text += message;
        }
        public void TrafficEnd(string message)
        {
            if (m_Disable) { return; }
            txtTraffic.Text += message + Environment.NewLine;
        }

        public bool Capabilities
        {
            get { return checkBoxCapabilities.Checked; }
        }
        public bool Other
        { get { return checkBoxAllOther.Checked; } }
        public bool Slew
        { get { return checkBoxSlew.Checked; } }
        public bool Gets
        { get { return checkBoxGets.Checked; } }
    }
}
