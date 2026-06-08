# Allow importing study tools and the shared x2uhj tools (rbj, analog_prototype).
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "tools")))
