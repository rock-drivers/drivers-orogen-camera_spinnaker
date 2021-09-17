require 'orocos'
require 'readline'
require 'utilrb'

include Orocos

ENV['PKG_CONFIG_PATH'] = "#{File.expand_path("..", File.dirname(__FILE__))}/build:#{ENV['PKG_CONFIG_PATH']}"

Orocos::CORBA::max_message_size = 100000000000
Orocos.initialize

Orocos::Process.run 'camera_spinnaker::Task' => 'camera_spinnaker' do

    # log all the output ports
    #Orocos.log_all_ports 

    # Get the task
    driver = Orocos.name_service.get 'camera_spinnaker'
    Orocos.conf.apply(driver, ['default'], :override => true)

    # Configure
    driver.configure

    # Start
    driver.start

    Readline::readline("Press ENTER to exit\n") do
    end
end
