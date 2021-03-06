import React, { Component } from 'react';
import { Slider } from '@material-ui/core';
import { withStyles } from "@material-ui/core/styles";
import Snackbar from '@material-ui/core/Snackbar';
import Switch from '@material-ui/core/Switch';
import { sendCommand } from './blind-functions';

import IconButton from '@material-ui/core/IconButton';
import UpIcon from '@material-ui/icons/ArrowUpward';
import DownIcon from '@material-ui/icons/ArrowDownward';
import SaveIcon from '@material-ui/icons/Save';

const UP_FOREVER_COMMAND = 104;
const DOWN_FOREVER_COMMAND = 102;
const STORE_COMMAND = 103;

class BlindsetSlider extends Component {

  constructor(props) {
    super(props);
    this.up = this.up.bind(this);
    this.down = this.down.bind(this);
    this.store = this.store.bind(this);
  }

  up() {
    console.log("Up: ", this.props.blindNumber);
    sendCommand(this.props.blindNumber, UP_FOREVER_COMMAND);
  }

  down() {
    console.log("Down: ", this.props.blindNumber);
    sendCommand(this.props.blindNumber, DOWN_FOREVER_COMMAND);
  }

  store() {
    console.log("Store: ", this.props.blindNumber);
    if(confirm("Store the current position as the bottom position?")) {
      sendCommand(this.props.blindNumber, STORE_COMMAND);
    }
  }

  render() {
    return <li className="blind-settings-buttons-container">
      <div className="blindset-name">#{this.props.blindNumber}</div>
      <IconButton className="settings-button" fontSize="small" onClick={this.up} aria-label="up" size="small">
        <UpIcon />
      </IconButton>
      <IconButton className="settings-button" fontSize="small" onClick={this.down} aria-label="up" size="small">
        <DownIcon />
      </IconButton>
      <IconButton className="settings-button" fontSize="small" onClick={this.store} aria-label="up" size="small">
        <SaveIcon />
      </IconButton>
    </li>
  }

}

export default BlindsetSlider;
